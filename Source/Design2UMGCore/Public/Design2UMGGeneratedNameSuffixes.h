// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

/**
 * Single source for generated-widget name suffixes. The Core conversion chain
 * appends these suffixes when it synthesizes wrapper/layer widgets, and the
 * Editor structure snapshot classifies carrier widgets by matching the same
 * literals - keeping them here turns producer/validator drift into a compile
 * error. Conversion-internal suffixes that no validator matches (_root, _sz,
 * _inner, _blur) stay local to their producers.
 */
namespace Design2UMGGeneratedNameSuffixes
{
	/** WidgetBlueprint asset-name prefix stamped on the converted root ("WBP_", FDesign2UMGConverter::Convert). */
	DESIGN2UMGCORE_API const TCHAR* WidgetBlueprintPrefix();

	/** RetainerBox foreground-blur wrapper suffix (VisualMapper::WrapWithForegroundBlur). */
	DESIGN2UMGCORE_API const TCHAR* FxBlur();
	/** RetainerBox geometry-mask wrapper suffix (VisualMapper::WrapWithGeometryMask). */
	DESIGN2UMGCORE_API const TCHAR* GeometryMask();
	/** RetainerBox inner-shadow wrapper suffix (VisualMapper::WrapWithInnerShadow). */
	DESIGN2UMGCORE_API const TCHAR* InnerShadow();

	/** Border explicit-stroke surface suffix (VisualMapperStrokeSupport). */
	DESIGN2UMGCORE_API const TCHAR* Surface();
	/** Content child suffix used when a wrapper takes over the visual (fill transfer, box shadow, prototype navigation). */
	DESIGN2UMGCORE_API const TCHAR* Content();

	/** Overlay explicit side-stroke child suffixes (VisualMapperStrokeSupport). */
	DESIGN2UMGCORE_API const TCHAR* StrokeTop();
	DESIGN2UMGCORE_API const TCHAR* StrokeRight();
	DESIGN2UMGCORE_API const TCHAR* StrokeBottom();
	DESIGN2UMGCORE_API const TCHAR* StrokeLeft();

	/** Indexed multi-fill layer suffix marker ("_fill"), matched before the index digits. */
	DESIGN2UMGCORE_API const TCHAR* FillMarker();
	/** Image-child suffix appended after a fill layer index ("_img"). */
	DESIGN2UMGCORE_API const TCHAR* FillImageSuffix();
	/** Indexed outer-shadow layer suffix marker ("_shadow"). */
	DESIGN2UMGCORE_API const TCHAR* ShadowMarker();

	/** Full indexed fill layer suffix, e.g. "_fill0". */
	DESIGN2UMGCORE_API FString Fill(const int32 Index);
	/** Full indexed fill-image child suffix, e.g. "_fill0_img". */
	DESIGN2UMGCORE_API FString FillImage(const int32 Index);
	/** Full indexed shadow layer suffix, e.g. "_shadow0". */
	DESIGN2UMGCORE_API FString Shadow(const int32 Index);
}
