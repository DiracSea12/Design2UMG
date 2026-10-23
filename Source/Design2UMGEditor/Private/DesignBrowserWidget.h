// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UWebBrowser;
struct FUMGBrowserData;

namespace DesignBrowserWidget
{
	bool ApplyTemplate(UWebBrowser& Widget, const FUMGBrowserData& Data);
	TSharedPtr<FJsonObject> Serialize(const FUMGBrowserData& Data);
	void Parse(const TSharedPtr<FJsonObject>& NodeObject, FUMGBrowserData& OutData);
}
