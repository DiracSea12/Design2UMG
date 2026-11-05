// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "UMGSchema.h"
#include "UMGWidgetTypes.h"

namespace UMGTextPaintOwner
{
	// Paint, input and glyph-shadow containers each have one content child.
	// Local endpoint containers identify their source child explicitly; other branches remain ambiguous.
	template <typename NodeType>
	NodeType* Find(NodeType& Root)
	{
		NodeType* Node = &Root;
		while (true)
		{
			if (Node->Effect.TextPaints.IsSet())
				return Node->Type == UMGWidgetTypes::RetainerBox && Node->Children.Num() == 1
					&& Node->Children[0].Type == UMGWidgetTypes::TextBlock ? Node : nullptr;
			if (Node->SourceVisualChildIndex != INDEX_NONE)
			{
				if (!Node->Children.IsValidIndex(Node->SourceVisualChildIndex)) return nullptr;
				Node = &Node->Children[Node->SourceVisualChildIndex];
				continue;
			}
			if (Node->Children.Num() != 1) return nullptr;
			Node = &Node->Children[0];
		}
	}
}
