// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
class FJsonObject;

/** Source uniform values; the GLSL declaration determines the binding type. */
struct FDesignShaderValue
{
    enum class EKind : uint8 { Numbers, Boolean, String };
    EKind Kind = EKind::Numbers;
    TArray<double> Numbers;
    bool Boolean = false;
    FString String;
};
struct FDesignShaderData
{
    FString Url;
    FString BlendMode;
    TMap<FString, FDesignShaderValue> Uniforms;
    FVector2D Size = FVector2D::ZeroVector;
    float Opacity = 1.0f;
    // Generated paint role: fill samples precede children and source effects.
    bool bFillPaint = false;
    // Logical left/top/right/bottom capture outsets, generated from the node paint bounds.
    FVector4 CaptureInsets = FVector4(0,0,0,0);
};
namespace DesignShaderJson
{
    DESIGN2UMGCORE_API TSharedRef<FJsonObject> Serialize(const FDesignShaderData& Shader);
    DESIGN2UMGCORE_API bool Parse(const TSharedPtr<FJsonObject>& Object, FDesignShaderData& Out);
}
