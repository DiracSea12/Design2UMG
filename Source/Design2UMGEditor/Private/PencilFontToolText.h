// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#pragma once
#include "CoreMinimal.h"

// Only the optional font tool's editable text resource; reload at panel/request boundaries.
class FPencilFontToolText
{
public:
    FPencilFontToolText();
    FString Get(const TCHAR* Key, const TMap<FString, FString>& Arguments = {}) const;
private:
    FString ResourcePath, Error;
    TMap<FString, FString> Values;
};
