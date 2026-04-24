// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetTree;
class FJsonObject;
struct FDesignDocument;
struct FDesignNode;

namespace Design2UMGAutomationStructureSnapshot
{
	TSharedPtr<FJsonObject> BuildDesignStructureSnapshot(
		const FDesignDocument& Doc,
		const FString& PreferredRootId = FString(),
		TFunction<bool(const TSharedPtr<FDesignNode>&)> AdditionalSkipNodePredicate = nullptr);
	TSharedPtr<FJsonObject> BuildWidgetTreeStructureSnapshot(UWidgetTree* WidgetTree);
}
