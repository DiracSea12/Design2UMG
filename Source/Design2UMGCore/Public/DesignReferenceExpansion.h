// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FDesignNode;

namespace RefExpander
{
	DESIGN2UMGCORE_API TSharedPtr<FDesignNode> ExpandRef(const TSharedPtr<FDesignNode>& RefNode, const FDesignDocument& Doc);
	DESIGN2UMGCORE_API TSharedPtr<FDesignNode> DeepCopyNode(const TSharedPtr<FDesignNode>& Source);
}
