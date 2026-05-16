// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchemaSourceExtensions.h"
#include "Extensions/WidgetBlueprintGeneratedClassExtension.h"
#include "WidgetBlueprintExtension.h"
#include "Design2UMGFigmaMetadataExtensions.generated.h"

class UWidgetBlueprintGeneratedClass;
class UWidgetTree;
class FWidgetBlueprintCompilerContext;

USTRUCT()
struct FDesign2UMGFigmaWidgetMetadataRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FName WidgetName = NAME_None;

	UPROPERTY()
	FString WidgetId;

	UPROPERTY()
	FUMGSourceNodeData SourceNode;

	bool IsValid() const
	{
		return WidgetName != NAME_None && SourceNode.HasAnyComponentSemantics();
	}
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGFigmaMetadataClassExtension
	: public UWidgetBlueprintGeneratedClassExtension
{
	GENERATED_BODY()

public:
	void SetMetadata(
		const FUMGSourceDocumentData& InDocumentData,
		const TArray<FDesign2UMGFigmaWidgetMetadataRecord>& InWidgetRecords);

	const FUMGSourceDocumentData& GetDocumentData() const { return DocumentData; }
	const TArray<FDesign2UMGFigmaWidgetMetadataRecord>& GetWidgetRecords() const { return WidgetRecords; }

	const FDesign2UMGFigmaWidgetMetadataRecord* FindRecordBySourceNodeId(const FString& SourceNodeId) const;
	const FDesign2UMGFigmaWidgetMetadataRecord* FindRecordByWidgetName(FName WidgetName) const;

#if WITH_EDITOR
	bool VerifyBindings(const UWidgetTree* WidgetTree) const;
#endif

private:
	UPROPERTY()
	FUMGSourceDocumentData DocumentData;

	UPROPERTY()
	TArray<FDesign2UMGFigmaWidgetMetadataRecord> WidgetRecords;
};

UCLASS(Within = WidgetBlueprint)
class DESIGN2UMGEDITOR_API UDesign2UMGFigmaMetadataBlueprintExtension
	: public UWidgetBlueprintExtension
{
	GENERATED_BODY()

public:
	void SetMetadata(
		const FUMGSourceDocumentData& InDocumentData,
		const TArray<FDesign2UMGFigmaWidgetMetadataRecord>& InWidgetRecords);

protected:
	virtual void HandleBeginCompilation(FWidgetBlueprintCompilerContext& InCreationContext) override;
	virtual void HandleFinishCompilingClass(UWidgetBlueprintGeneratedClass* Class) override;
	virtual bool HandleValidateGeneratedClass(UWidgetBlueprintGeneratedClass* Class) override;
	virtual void HandleEndCompilation() override;

private:
	bool HasAnyMetadata() const;

	FWidgetBlueprintCompilerContext* CompilerContext = nullptr;

	UPROPERTY()
	FUMGSourceDocumentData DocumentData;

	UPROPERTY()
	TArray<FDesign2UMGFigmaWidgetMetadataRecord> WidgetRecords;
};
