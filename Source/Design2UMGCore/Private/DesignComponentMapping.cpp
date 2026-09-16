// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignComponentMapping.h"

#include "UMGSchemaSourceExtensions.h"

namespace DesignComponentMapping
{
	FString FDesignComponentIdentity::BuildVariantKey() const
	{
		return Provider + TEXT("|") + DocumentScope + TEXT("|") + ComponentKey;
	}

	FString FDesignComponentIdentity::BuildSetKey() const
	{
		return Provider + TEXT("|") + DocumentScope + TEXT("|") + ComponentSetKey;
	}

	FDocumentScopeResolution ResolveFigmaDocumentScope(
		const FString& DocumentFileKey,
		const FString& ImportSourceIdentifier)
	{
		FDocumentScopeResolution Resolution;
		if (!DocumentFileKey.IsEmpty())
		{
			Resolution.Value = DocumentFileKey;
			Resolution.Source = TEXT("fileKey");
			return Resolution;
		}
		if (!ImportSourceIdentifier.IsEmpty())
		{
			Resolution.Value = ImportSourceIdentifier;
			Resolution.Source = TEXT("importSource");
			return Resolution;
		}
		Resolution.Source = TEXT("unresolved");
		return Resolution;
	}

	bool TryResolveFigmaComponentIdentity(
		const FUMGSourceNodeData& SourceNode,
		const FString& DocumentScope,
		FDesignComponentIdentity& OutIdentity)
	{
		OutIdentity = FDesignComponentIdentity();
		if (DocumentScope.IsEmpty())
		{
			return false;
		}

		FString ComponentKey;
		if (SourceNode.IsFigmaInstance())
		{
			// Figma mainComponentKey is stable across exports, so prefer it; fall back to componentId when absent.
			ComponentKey = !SourceNode.MainComponentKey.IsEmpty()
				? SourceNode.MainComponentKey
				: SourceNode.ComponentId;
		}
		else if (SourceNode.IsFigmaComponentDefinition())
		{
			// Component definition node: the mirroring chain has backfilled its own Id into ComponentId.
			ComponentKey = SourceNode.ComponentId;
		}

		if (ComponentKey.IsEmpty())
		{
			return false;
		}

		OutIdentity.Provider = ProviderFigma;
		OutIdentity.DocumentScope = DocumentScope;
		OutIdentity.ComponentKey = ComponentKey;
		OutIdentity.ComponentSetKey = SourceNode.ComponentSetKey;
		return true;
	}

	const FComponentMappingEntry* FComponentMappingTable::Match(
		const FDesignComponentIdentity& Identity,
		bool& bOutMatchedAtSetLevel) const
	{
		bOutMatchedAtSetLevel = false;
		if (!Identity.IsValid() || Entries.Num() == 0)
		{
			return nullptr;
		}

		// Tier 1: exact variant-level Key match (one mapping per variant).
		const FString VariantKey = Identity.BuildVariantKey();
		for (const FComponentMappingEntry& Entry : Entries)
		{
			if (Entry.Key.Equals(VariantKey, ESearchCase::IgnoreCase))
			{
				return &Entry;
			}
		}

		// Tier 2: component-set-level Key match (one mapping per set; concrete variant values arrive via variant inputs).
		if (!Identity.ComponentSetKey.IsEmpty())
		{
			const FString SetKey = Identity.BuildSetKey();
			for (const FComponentMappingEntry& Entry : Entries)
			{
				if (Entry.Key.Equals(SetKey, ESearchCase::IgnoreCase))
				{
					bOutMatchedAtSetLevel = true;
					return &Entry;
				}
			}
		}

		return nullptr;
	}

	const FComponentMappingEntry* FComponentMappingTable::FindEntryByKey(const FString& Key) const
	{
		for (const FComponentMappingEntry& Entry : Entries)
		{
			if (Entry.Key.Equals(Key, ESearchCase::IgnoreCase))
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	bool ParseMappingKey(
		const FString& Key,
		FString& OutProvider,
		FString& OutDocumentScope,
		FString& OutComponentKey)
	{
		OutProvider.Reset();
		OutDocumentScope.Reset();
		OutComponentKey.Reset();

		TArray<FString> Parts;
		Key.ParseIntoArray(Parts, TEXT("|"), true);
		if (Parts.Num() != 3)
		{
			return false;
		}

		OutProvider = Parts[0];
		OutDocumentScope = Parts[1];
		OutComponentKey = Parts[2];
		return !OutProvider.IsEmpty() && !OutDocumentScope.IsEmpty() && !OutComponentKey.IsEmpty();
	}

	bool ParseComponentInputType(const FString& Type, EComponentInputType& OutType)
	{
		if (Type.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
		{
			OutType = EComponentInputType::Text;
			return true;
		}
		if (Type.Equals(TEXT("Bool"), ESearchCase::IgnoreCase))
		{
			OutType = EComponentInputType::Bool;
			return true;
		}
		if (Type.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
		{
			OutType = EComponentInputType::Name;
			return true;
		}
		if (Type.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
		{
			OutType = EComponentInputType::Float;
			return true;
		}
		return false;
	}

	FString GetComponentInputTypeName(EComponentInputType Type)
	{
		switch (Type)
		{
		case EComponentInputType::Text: return TEXT("Text");
		case EComponentInputType::Bool: return TEXT("Bool");
		case EComponentInputType::Name: return TEXT("Name");
		case EComponentInputType::Float: return TEXT("Float");
		default: return FString();
		}
	}

	bool ParseComponentInputValueKind(const FString& ValueKind, EComponentInputValueKind& OutValueKind)
	{
		if (ValueKind.Equals(TEXT("ComponentProperty"), ESearchCase::IgnoreCase))
		{
			OutValueKind = EComponentInputValueKind::ComponentProperty;
			return true;
		}
		if (ValueKind.Equals(TEXT("ComponentText"), ESearchCase::IgnoreCase))
		{
			OutValueKind = EComponentInputValueKind::ComponentText;
			return true;
		}
		return false;
	}

	FString GetComponentInputValueKindName(EComponentInputValueKind ValueKind)
	{
		switch (ValueKind)
		{
		case EComponentInputValueKind::ComponentProperty: return TEXT("ComponentProperty");
		case EComponentInputValueKind::ComponentText: return TEXT("ComponentText");
		default: return FString();
		}
	}

	bool TryParseComponentInputDeclaration(
		const FString& Source,
		const FString& Target,
		const FString& Type,
		const FString& ValueKind,
		FComponentInputDeclaration& OutDeclaration,
		FString& OutError)
	{
		OutDeclaration = FComponentInputDeclaration();
		if (Source.IsEmpty() || Target.IsEmpty())
		{
			OutError = TEXT("Source and Target must both be non-empty");
			return false;
		}
		if (!ParseComponentInputType(Type, OutDeclaration.Type))
		{
			OutError = FString::Printf(
				TEXT("Type '%s' is not one of Text/Bool/Name/Float"),
				*Type);
			return false;
		}

		// ValueKind may be omitted; it defaults to the componentProperties key value. When declared explicitly it must be inside the closed set.
		if (ValueKind.IsEmpty())
		{
			OutDeclaration.ValueKind = EComponentInputValueKind::ComponentProperty;
		}
		else if (!ParseComponentInputValueKind(ValueKind, OutDeclaration.ValueKind))
		{
			OutError = FString::Printf(
				TEXT("ValueKind '%s' is not one of ComponentProperty/ComponentText"),
				*ValueKind);
			return false;
		}

		OutDeclaration.Source = Source;
		OutDeclaration.Target = Target;
		return true;
	}
}
