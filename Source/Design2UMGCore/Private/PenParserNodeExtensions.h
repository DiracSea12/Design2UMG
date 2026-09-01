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
}
