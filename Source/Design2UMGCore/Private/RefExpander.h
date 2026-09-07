// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignReferenceExpansion.h"
#include "DesignVariableResolver.h"

namespace RefExpander
{
	void ReThemeNode(TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver);
	void ApplyDescendantOverride(TSharedPtr<FDesignNode>& Node, const FString& Path, const TSharedPtr<FDesignNode>& Override);
}
