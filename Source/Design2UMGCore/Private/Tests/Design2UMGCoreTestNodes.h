// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "DesignSchema.h"

// Shared FDesignNode construction helper for Design2UMGCore automation tests.
// Sets node identity (Id/Name) and marks the given sizes as authored source sizes
// (bHas*/bSourceHas*). Include only from Core test .cpp files.

namespace Design2UMGCoreTests
{
	inline TSharedPtr<FDesignNode> MakeNode(
		const FString& Id,
		const EDesignNodeType Type,
		const FDesignSize& Width = FDesignSize::Fixed(120.0f),
		const FDesignSize& Height = FDesignSize::Fixed(40.0f))
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = Type;
		Node->Transform.SetWidthValue(Width);
		Node->Transform.SetHeightValue(Height);
		return Node;
	}
}
