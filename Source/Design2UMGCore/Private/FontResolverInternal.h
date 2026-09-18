// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "FontResolver.h"

namespace FontResolverInternal
{
enum class EExplicitFontMappingResult : uint8
{
	NoMapping,
	Resolved,
	Invalid
};

FString NormalizeFontLookupKey(const FString& InValue);

bool IsProjectFontAssetPath(const FString& ObjectPath);

bool IsEngineFontAssetPath(const FString& ObjectPath);

EExplicitFontMappingResult TryResolveExplicitFontMapping(const FString& Family, const FString& PostScriptName, FString& OutPath, const FDesign2UMGFontMappings* ExplicitMappings = nullptr);

bool HasExplicitFontMappingKey(const FString& Key, const FDesign2UMGFontMappings* ExplicitMappings = nullptr);
}
