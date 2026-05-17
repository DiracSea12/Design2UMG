// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeRouting.h"

#include "Misc/Crc.h"

namespace Design2UMGPrototypeRouting
{
	void CollectPrototypeReservedTokens(
		const TSharedPtr<FDesignNode>& Node,
		TSet<FString>& OutReservedTokens)
	{
		if (!Node.IsValid())
		{
			return;
		}

		if (!Node->Id.IsEmpty())
		{
			OutReservedTokens.Add(Node->Id);
		}

		if (!Node->Name.IsEmpty())
		{
			OutReservedTokens.Add(Node->Name);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			CollectPrototypeReservedTokens(Child, OutReservedTokens);
		}
	}

	uint32 BuildPrototypeInternalTokenHash(
		const TArray<TSharedPtr<FDesignNode>>& PrototypePages)
	{
		FString HashSource;
		for (const TSharedPtr<FDesignNode>& PageNode : PrototypePages)
		{
			if (!PageNode.IsValid())
			{
				continue;
			}

			HashSource += PageNode->Id;
			HashSource += TEXT("|");
			HashSource += PageNode->Name;
			HashSource += TEXT(";");
		}

		return FCrc::StrCrc32(*HashSource);
	}

	FString SanitizePrototypeTokenSuffix(const FString& Suffix)
	{
		FString Sanitized;
		Sanitized.Reserve(Suffix.Len());

		for (const TCHAR Character : Suffix)
		{
			if (FChar::IsAlnum(Character))
			{
				Sanitized.AppendChar(FChar::ToLower(Character));
			}
			else
			{
				Sanitized.AppendChar(TEXT('_'));
			}
		}

		while (Sanitized.ReplaceInline(TEXT("__"), TEXT("_"), ESearchCase::CaseSensitive) > 0)
		{
		}

		Sanitized.TrimCharInline(TEXT('_'), nullptr);
		if (Sanitized.IsEmpty())
		{
			return TEXT("node");
		}

		return Sanitized;
	}

	FString MakeUniquePrototypeInternalToken(
		const TSet<FString>& ReservedTokens,
		const uint32 PrototypeInternalTokenHash,
		const FString& Suffix)
	{
		const FString SanitizedSuffix = SanitizePrototypeTokenSuffix(Suffix);
		const FString BaseToken = FString::Printf(
			TEXT("__d2u_prototype_%s_%08x"),
			*SanitizedSuffix,
			PrototypeInternalTokenHash);

		FString Candidate = BaseToken;
		int32 CollisionIndex = 0;
		while (ReservedTokens.Contains(Candidate))
		{
			++CollisionIndex;
			Candidate = FString::Printf(TEXT("%s_%d"), *BaseToken, CollisionIndex);
		}

		return Candidate;
	}

	void MapNodeIdsToTopLevelPage(
		const TSharedPtr<FDesignNode>& Node,
		const FString& PageId,
		TMap<FString, FString>& OutNodeToPageId)
	{
		if (!Node.IsValid())
		{
			return;
		}

		if (!Node->Id.IsEmpty() && !OutNodeToPageId.Contains(Node->Id))
		{
			OutNodeToPageId.Add(Node->Id, PageId);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			MapNodeIdsToTopLevelPage(Child, PageId, OutNodeToPageId);
		}
	}

	FPrototypeRoutingContext BuildRoutingContext(
		const TArray<TSharedPtr<FDesignNode>>& PrototypePages)
	{
		FPrototypeRoutingContext Context;
		for (const TSharedPtr<FDesignNode>& PageNode : PrototypePages)
		{
			CollectPrototypeReservedTokens(PageNode, Context.ReservedTokens);
		}

		Context.InternalTokenHash = BuildPrototypeInternalTokenHash(PrototypePages);

		if (PrototypePages.Num() <= 1)
		{
			return Context;
		}

		for (int32 PageIndex = 0; PageIndex < PrototypePages.Num(); ++PageIndex)
		{
			const TSharedPtr<FDesignNode>& PageNode = PrototypePages[PageIndex];
			if (!PageNode.IsValid() || PageNode->Id.IsEmpty())
			{
				continue;
			}

			Context.PageIds.Add(PageNode->Id);
			Context.PageIdToIndex.Add(PageNode->Id, PageIndex);
			MapNodeIdsToTopLevelPage(PageNode, PageNode->Id, Context.NodeToPageId);
		}

		return Context;
	}
}
