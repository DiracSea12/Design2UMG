// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

namespace UMGJsonKeys
{
	// Structural JSON field names shared by the Editor UMG JSON parser and
	// serializer: nested widget-node section fields (gradient, image, gradient
	// stops, box-shadow layers) plus the TextStyle SourceFontSize payload
	// field. Both sides must keep these values byte-identical.
	inline constexpr const TCHAR* Type = TEXT("Type");
	inline constexpr const TCHAR* Mode = TEXT("Mode");
	inline constexpr const TCHAR* Transform = TEXT("Transform");
	inline constexpr const TCHAR* CropFitMode = TEXT("CropFitMode");
	inline constexpr const TCHAR* Rotation = TEXT("Rotation");
	inline constexpr const TCHAR* StartPoint = TEXT("StartPoint");
	inline constexpr const TCHAR* EndPoint = TEXT("EndPoint");
	inline constexpr const TCHAR* AngularBasis = TEXT("AngularBasis");
	inline constexpr const TCHAR* Center = TEXT("Center");
	inline constexpr const TCHAR* Size = TEXT("Size");
	inline constexpr const TCHAR* Stops = TEXT("Stops");
	inline constexpr const TCHAR* Position = TEXT("Position");
	inline constexpr const TCHAR* BoxShadows = TEXT("BoxShadows");
inline constexpr const TCHAR* BlendMode = TEXT("BlendMode");
inline constexpr const TCHAR* PaintBlendMode = TEXT("PaintBlendMode");
inline constexpr const TCHAR* InnerShadowBlendMode = TEXT("InnerShadowBlendMode");
inline constexpr const TCHAR* BackgroundCaptureInsets = TEXT("BackgroundCaptureInsets");
	inline constexpr const TCHAR* Offset = TEXT("Offset");
	inline constexpr const TCHAR* Blur = TEXT("Blur");
	inline constexpr const TCHAR* Spread = TEXT("Spread");
	inline constexpr const TCHAR* ShowShadowBehindNode = TEXT("ShowShadowBehindNode");
	inline constexpr const TCHAR* SourceFontSize = TEXT("SourceFontSize");
}
