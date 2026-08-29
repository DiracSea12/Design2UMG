// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"
#include "UMGWidgetNodeProceduralResourceAccess.h"

class UImage;
class UObject;
class UTexture2D;

class FWidgetResourceBinder
{
public:
	// Prefer material first, then fall back to texture; returns whether any resource was bound.
	static bool BindImageResource(UImage* ImageWidget, const FUMGWidgetNode& Node, const TMap<FString, UTexture2D*>* TextureMap);

	// Outcome of the shared procedural-texture resolution step. Both image-binding call sites
	// (brush property application and this binder) resolve through TryResolveProceduralTexture
	// so the request build and material-ref gate stay in one place.
	enum class EProceduralTextureResult : uint8
	{
		NoRequest,        // node carries no procedural material request
		EmptyMaterialRef, // request exists but its material ref is empty
		UnknownShape,     // request is valid but no texture could be generated
		Resolved
	};

	static EProceduralTextureResult TryResolveProceduralTexture(
		const FUMGWidgetNode& Node,
		UObject* Outer,
		UTexture2D*& OutTexture,
		FUMGProceduralResourceRequest* OutRequest = nullptr);
};
