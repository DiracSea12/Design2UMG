// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "SharedCoreSemantics.h"

namespace
{
	bool HasVisibleFill(const FDesignFill& Fill)
	{
		return Fill.bEnabled && Fill.Type != EDesignFillType::None;
	}
}

namespace Design2UMGCore::SharedSemantics
{
	bool IsDesignerOnlyNodeType(EDesignNodeType Type)
	{
		return Type == EDesignNodeType::Connection
			|| Type == EDesignNodeType::Note
			|| Type == EDesignNodeType::Prompt
			|| Type == EDesignNodeType::Context;
	}

	bool HasRenderableVisual(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
		{
			return true;
		}

		if (HasVisibleFill(Node->Visual.Fill))
		{
			return true;
		}

		for (const FDesignFill& Fill : Node->Visual.Fills)
		{
			if (HasVisibleFill(Fill))
			{
				return true;
			}
		}

		if (Node->Visual.Stroke.HasStroke())
		{
			return true;
		}

		for (const FDesignEffect& Effect : Node->Visual.Effects)
		{
			if (Effect.bEnabled)
			{
				return true;
			}
		}

		return false;
	}

	bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->Transform.bEnabled || IsDesignerOnlyNodeType(Node->Type))
		{
			return true;
		}

		// Ref nodes must stay in the tree until expansion resolves their real content.
		if (Node->Type == EDesignNodeType::Ref)
		{
			return false;
		}

		// Keep explicit flow containers (including empty GRID/WRAP/H/V frames) so layout semantics are not dropped.
		if (Node->Children.Num() == 0 && !HasRenderableVisual(Node))
		{
			const bool bIsExplicitFlowContainer =
				(Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
				&& Node->LayoutData.Layout != EDesignLayout::None;
			if (!bIsExplicitFlowContainer)
			{
				return true;
			}
		}

		return false;
	}

	float ResolveWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		if (Node->Transform.Width.IsFill())
		{
			return Node->Transform.Width.Value > 0.0f ? Node->Transform.Width.Value : ParentContentWidth;
		}

		return Node->Transform.Width.Value > 0.0f ? Node->Transform.Width.Value : 0.0f;
	}
}

