// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignNodeSemantics.h"

namespace Design2UMGCore::DesignNodeSemantics
{
	namespace
	{
		bool IsTextLikeNode(const TSharedPtr<FDesignNode>& Node)
		{
			return Node.IsValid()
				&& (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont);
		}
	}

	bool HasVisibleFill(const FDesignFill& Fill)
	{
		return Fill.bEnabled && Fill.Type != EDesignFillType::None;
	}

	bool IsDesignerOnlyNodeType(EDesignNodeType Type)
	{
		// Note/Context/Prompt sticky nodes render as sticky cards on the Pencil
		// canvas and are synthesized into card subtrees by the pen parser, so
		// they must stay convertible; only canvas connections are chrome-only.
		return Type == EDesignNodeType::Connection;
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

	bool TryGetConcreteSourceTextBoxSize(const TSharedPtr<FDesignNode>& Node, float& OutWidth, float& OutHeight)
	{
		if (!IsTextLikeNode(Node))
		{
			return false;
		}

		const bool bHasFinitePositiveSourceSize =
			Node->Transform.bSourceHasWidth
			&& Node->Transform.bSourceHasHeight
			&& FMath::IsFinite(Node->Transform.Width.Value)
			&& FMath::IsFinite(Node->Transform.Height.Value)
			&& Node->Transform.Width.Value > 0.0f
			&& Node->Transform.Height.Value > 0.0f;
		if (!bHasFinitePositiveSourceSize)
		{
			return false;
		}

		if (!Node->Transform.Width.IsFixed() || !Node->Transform.Height.IsFixed())
		{
			return false;
		}

		OutWidth = Node->Transform.Width.Value;
		OutHeight = Node->Transform.Height.Value;
		return true;
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

