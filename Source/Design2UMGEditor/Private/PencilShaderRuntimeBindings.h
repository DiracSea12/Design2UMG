// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"
class UWidgetBlueprint;
namespace PencilShaderRuntimeBindings
{
// Creates native paint-boundary assets before the parent blueprint skeleton refresh.
bool Prepare(UWidgetBlueprint* Blueprint, FString& Error, TArray<FString>* GeneratedNames = nullptr);
} // namespace PencilShaderRuntimeBindings
