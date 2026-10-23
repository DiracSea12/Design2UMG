// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignGeometryPath
{
	FString Path;
	FString FillRule;

	friend FArchive& operator<<(FArchive& Archive, FDesignGeometryPath& Geometry)
	{
		return Archive << Geometry.Path << Geometry.FillRule;
	}
};

struct FDesignPaintGeometry
{
	// Source presence distinguishes known empty geometry from unexported geometry.
	TOptional<TArray<FDesignGeometryPath>> FillPaths;
	TOptional<TArray<FDesignGeometryPath>> StrokePaths;

	friend FArchive& operator<<(FArchive& Archive, FDesignPaintGeometry& Geometry)
	{
		return Archive << Geometry.FillPaths << Geometry.StrokePaths;
	}
};
