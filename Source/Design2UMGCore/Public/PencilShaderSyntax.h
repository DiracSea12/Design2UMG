// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
#include <string>
#include <vector>
struct FDesignShaderData;
namespace PencilShaderSyntax
{
struct FUniform
{
    std::string Name, Type, DefaultValue, SizeOf, MappedName;
    bool Time=false, Resolution=false, Mouse=false, Sdf=false, Mipmap=false;
    int TextureIndex=-1;
};
struct FProgram
{
    std::string Source, Code;
    bool UsesCoordinates=false;
    std::vector<FUniform> Uniforms;
};
DESIGN2UMGCORE_API FString ResolveStringUniform(const FDesignShaderData& Shader, const FUniform& Uniform);
DESIGN2UMGCORE_API bool PrepareSource(const std::string& Source,FProgram& Out,std::string& Error);
DESIGN2UMGCORE_API bool WrapHlsl(const std::string& Hlsl,FProgram& Program,std::string& Error);
}
