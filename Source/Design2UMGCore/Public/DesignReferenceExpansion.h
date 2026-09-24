// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;
struct FDesignNode;

enum class EDesignReferenceExpansionStatus : uint8
{
	Succeeded,
	MissingTarget,
	CircularReference,
	ChainLimitExceeded,
	InvalidReference
};

/** Caller-visible outcome for one reference expansion attempt. */
struct DESIGN2UMGCORE_API FDesignReferenceExpansionResult
{
	TSharedPtr<FDesignNode> ExpandedNode;
	EDesignReferenceExpansionStatus Status = EDesignReferenceExpansionStatus::InvalidReference;
	FString NodeId;
	FString ReferencedNodeId;
	FString Message;

	bool IsSuccess() const
	{
		return Status == EDesignReferenceExpansionStatus::Succeeded && ExpandedNode.IsValid();
	}

	bool IsFatal() const
	{
		return Status == EDesignReferenceExpansionStatus::CircularReference
			|| Status == EDesignReferenceExpansionStatus::ChainLimitExceeded;
	}
};

namespace RefExpander
{
	DESIGN2UMGCORE_API FDesignReferenceExpansionResult ExpandRef(
		const TSharedPtr<FDesignNode>& RefNode,
		const FDesignDocument& Doc);
	DESIGN2UMGCORE_API TSharedPtr<FDesignNode> DeepCopyNode(const TSharedPtr<FDesignNode>& Source);
}
