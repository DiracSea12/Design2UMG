// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "Dom/JsonObject.h"

namespace PenParserNodeExtensions
{
	void FinalizePenPaddingRuntimeWrapPolicy(const TSharedPtr<FDesignNode>& Node);
	void ParsePenSemanticMetadata(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& Node, const FDesignVariableResolver& Resolver);
	void ParseNodeExtended(
		const TSharedPtr<FJsonObject>& JsonObj,
		TSharedPtr<FDesignNode>& Node,
		const FDesignVariableResolver& Resolver);
	/**
	 * Converts a designer sticky node (note/context/prompt) into the sticky-card
	 * visual subtree the Pencil canvas renders for it (header band, body text,
	 * prompt-only copy footer). Must run after ParseNodeExtended so the sticky
	 * text payload is already parsed into Node->Text.
	 */
	void SynthesizeStickyCard(const TSharedPtr<FDesignNode>& Node);
}
