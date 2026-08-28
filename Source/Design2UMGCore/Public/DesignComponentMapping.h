// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FUMGSourceNodeData;

/**
 * Pure-logic component identity and project-level Component Mapping matching layer.
 *
 * Only string-level work happens here: identity extraction, DocumentScope resolution, mapping declaration
 * parsing, and two-tier matching. Class loading and instantiation happen only in the Editor module; this layer depends on no UE assets.
 */
namespace DesignComponentMapping
{
	/** Canonical provider values (shared repo-wide in Core): source.provider metadata, the IR document sidecar, and the Provider segment of mapping keys all use these two values. */
	constexpr const TCHAR* ProviderFigma = TEXT("figma");
	constexpr const TCHAR* ProviderPencil = TEXT("pencil");

	/** Closed v1 input type set: Text(FText) / Bool(bool) / Name(FName) / Float(double). */
	enum class EComponentInputType : uint8
	{
		Text,
		Bool,
		Name,
		Float
	};

	/** Closed v1 value sources: the component's own text / a componentProperties key value. */
	enum class EComponentInputValueKind : uint8
	{
		ComponentProperty,
		ComponentText
	};

	/** A single explicit design-facing input declaration (parsed and validated). */
	struct FComponentInputDeclaration
	{
		FString Source;
		FString Target;
		EComponentInputType Type = EComponentInputType::Text;
		EComponentInputValueKind ValueKind = EComponentInputValueKind::ComponentProperty;
	};

	/** A single project-level mapping declaration (Key/class-path format validated; class loadability is validated on the Editor side). */
	struct FComponentMappingEntry
	{
		FString Key;
		FString Provider;
		FString DocumentScope;
		FString ComponentKey;
		FString WidgetClass;
		TArray<FComponentInputDeclaration> Inputs;
	};

	/** Canonical component identity: Provider + DocumentScope + ComponentKey (optional ComponentSetKey). */
	struct FDesignComponentIdentity
	{
		FString Provider;
		FString DocumentScope;
		FString ComponentKey;
		FString ComponentSetKey;

		bool IsValid() const
		{
			return !Provider.IsEmpty() && !DocumentScope.IsEmpty() && !ComponentKey.IsEmpty();
		}

		/** Variant-level match key: Provider|DocumentScope|ComponentKey. */
		DESIGN2UMGCORE_API FString BuildVariantKey() const;
		/** Component-set-level match key: Provider|DocumentScope|ComponentSetKey. */
		DESIGN2UMGCORE_API FString BuildSetKey() const;
	};

	/** DocumentScope resolution result; Source tells which origin this scope came from, usable in diagnostics. */
	struct FDocumentScopeResolution
	{
		FString Value;
		FString Source; // "fileKey" | "importSource" | "unresolved"

		bool IsResolved() const { return !Value.IsEmpty(); }
	};

	/** The single unified DocumentScope resolution rule (exactly one in the whole repo):
	 *  use the file key when the Figma export JSON carries one at the top level; otherwise use the import source identifier (URL / file path).*/
	DESIGN2UMGCORE_API FDocumentScopeResolution ResolveFigmaDocumentScope(
		const FString& DocumentFileKey,
		const FString& ImportSourceIdentifier);

	/**
	 * Extract the Figma component identity from the UMG-side source-data sidecar.
	 * Rules: instance nodes take the component identity (mainComponentKey first, componentId when absent);
	 * component definition nodes take their own id as ComponentKey; ComponentSetKey always comes from componentSetId.
	 * Returns false when no non-empty ComponentKey can be extracted (not a mapped candidate).
	 */
	DESIGN2UMGCORE_API bool TryResolveFigmaComponentIdentity(
		const FUMGSourceNodeData& SourceNode,
		const FString& DocumentScope,
		FDesignComponentIdentity& OutIdentity);

	/** Two-tier match table. Exact variant-level Key match first; when missed and ComponentSetKey is non-empty, component-set-level Key match. No fuzzy matching of any kind. */
	struct FComponentMappingTable
	{
		TArray<FComponentMappingEntry> Entries;

		bool IsEmpty() const { return Entries.Num() == 0; }

		/** Returns the hit entry; bOutMatchedAtSetLevel tells whether the hit happened at component-set level. Returns nullptr on miss. */
		DESIGN2UMGCORE_API const FComponentMappingEntry* Match(
			const FDesignComponentIdentity& Identity,
			bool& bOutMatchedAtSetLevel) const;

		DESIGN2UMGCORE_API const FComponentMappingEntry* FindEntryByKey(const FString& Key) const;
	};

	/** Parse a mapping key string (Provider|DocumentScope|ComponentKey). All three segments must be non-empty to be valid. */
	DESIGN2UMGCORE_API bool ParseMappingKey(
		const FString& Key,
		FString& OutProvider,
		FString& OutDocumentScope,
		FString& OutComponentKey);

	/** Conversions between input type names and the closed set; illegal strings return false. */
	DESIGN2UMGCORE_API bool ParseComponentInputType(const FString& Type, EComponentInputType& OutType);
	DESIGN2UMGCORE_API FString GetComponentInputTypeName(EComponentInputType Type);

	/** Conversions between value-source names and the closed set; illegal strings return false. */
	DESIGN2UMGCORE_API bool ParseComponentInputValueKind(
		const FString& ValueKind,
		EComponentInputValueKind& OutValueKind);
	DESIGN2UMGCORE_API FString GetComponentInputValueKindName(EComponentInputValueKind ValueKind);

	/**
	 * Parse and validate one input declaration. Valid only when Source/Target are non-empty and Type/ValueKind belong to the closed set;
	 * returns false with a readable reason on failure (goes into diagnostics).
	 */
	DESIGN2UMGCORE_API bool TryParseComponentInputDeclaration(
		const FString& Source,
		const FString& Target,
		const FString& Type,
		const FString& ValueKind,
		FComponentInputDeclaration& OutDeclaration,
		FString& OutError);
}
