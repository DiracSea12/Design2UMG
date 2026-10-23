// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"

class FJsonObject;

/** Normalized grid positions and relative Bezier handles; colors are source RGBA bytes. */
struct FDesignMeshGradientVertex
{
    FVector2D Position = FVector2D::ZeroVector;
    FVector2D LeftHandle = FVector2D::ZeroVector;
    FVector2D RightHandle = FVector2D::ZeroVector;
    FVector2D TopHandle = FVector2D::ZeroVector;
    FVector2D BottomHandle = FVector2D::ZeroVector;
    FColor Color = FColor::Transparent;
};

struct FDesignMeshGradient
{
    int32 Columns = 0;
    int32 Rows = 0;
    float Opacity = 1.0f;
    TArray<FDesignMeshGradientVertex> Vertices;

    bool IsValid() const { return Columns >= 2 && Rows >= 2 && int64(Columns) * Rows == Vertices.Num(); }
    DESIGN2UMGCORE_API void WriteSignature(FArchive& Ar) const;

};

namespace DesignMeshGradientJson
{
    DESIGN2UMGCORE_API TSharedRef<FJsonObject> Serialize(const FDesignMeshGradient& Mesh);
    DESIGN2UMGCORE_API bool Parse(const TSharedPtr<FJsonObject>& Object, FDesignMeshGradient& OutMesh);
    DESIGN2UMGCORE_API FString SerializeIdentity(const FDesignMeshGradient& Mesh);
}
