// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
#include "DesignSchemaFoundation.h"
#include "DesignPathGeometry.h"

namespace DesignRoundedContour
{
	DESIGN2UMGCORE_API bool Build(const FVector2D& Size, const FDesignCornerRadius& Corners, FDesignGeometryPath& Path, FString& Error);
	DESIGN2UMGCORE_API bool BuildInsideStroke(const FVector2D& Size, const FDesignCornerRadius& Corners,
		const FDesignStrokeThickness& Thickness, FDesignGeometryPath& Path, FString& Error);
}
