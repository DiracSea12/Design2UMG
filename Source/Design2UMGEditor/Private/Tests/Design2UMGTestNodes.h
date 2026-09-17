// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchema.h"

// Shared FDesignNode construction helpers for Design2UMGEditor automation tests.
// The 7-argument MakeNode sets an explicit position plus fixed source sizes; the
// 5-argument overload keeps the same fixed-size contract but leaves ExplicitX/ExplicitY
// unset for fixtures that must not declare an authored position.
// Ref names are authored through RefOverrides rather than inferred from the full node state.

namespace Design2UMGEditorTests
{
	inline TSharedPtr<FDesignNode> MakeNode(
		const FString& Id,
		const FString& Name,
		const EDesignNodeType Type,
		const float X,
		const float Y,
		const float Width,
		const float Height)
	{
		const TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Name;
		Node->Type = Type;
		if (Type == EDesignNodeType::Ref)
		{
			TSharedPtr<FDesignNodePatch> Patch = MakeShared<FDesignNodePatch>();
			Patch->Name = Name;
			Node->RefOverrides = MoveTemp(Patch);
		}
		Node->Transform.SetPosition(X, Y);
		Node->Transform.SetWidthValue(FDesignSize::Fixed(Width));
		Node->Transform.SetHeightValue(FDesignSize::Fixed(Height));
		return Node;
	}

	// Position-free variant: same fixed-size contract, no explicit position flags.
	inline TSharedPtr<FDesignNode> MakeNode(
		const FString& Id,
		const FString& Name,
		const EDesignNodeType Type,
		const float Width,
		const float Height)
	{
		const TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Name;
		Node->Type = Type;
		if (Type == EDesignNodeType::Ref)
		{
			TSharedPtr<FDesignNodePatch> Patch = MakeShared<FDesignNodePatch>();
			Patch->Name = Name;
			Node->RefOverrides = MoveTemp(Patch);
		}
		Node->Transform.SetWidthValue(FDesignSize::Fixed(Width));
		Node->Transform.SetHeightValue(FDesignSize::Fixed(Height));
		return Node;
	}
}
