// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
#include "PencilShaderSyntax.h"
namespace PencilShaderCompiler
{
bool Compile(const FString& Source,PencilShaderSyntax::FProgram& Out,FString& Error);
}
