// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGFigmaMetadataExtensions.h"

#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Design2UMGFigmaMetadataExtensions)

void UDesign2UMGFigmaMetadataClassExtension::SetMetadata(
	const FUMGSourceDocumentData& InDocumentData,
	const TArray<FDesign2UMGFigmaWidgetMetadataRecord>& InWidgetRecords)
{
	DocumentData = InDocumentData;
	WidgetRecords.Reset();
	for (const FDesign2UMGFigmaWidgetMetadataRecord& Record : InWidgetRecords)
	{
		if (Record.IsValid())
		{
			WidgetRecords.Add(Record);
		}
	}
}

void UDesign2UMGFigmaMetadataClassExtension::PostLoad()
{
	Super::PostLoad();
	for (FDesign2UMGFigmaWidgetMetadataRecord& Record : WidgetRecords)
	{
		FUMGSourceNodeData& SN = Record.SourceNode;
		if (!SN.HasIsExposedInstance.bIsSet)
		{
			SN.HasIsExposedInstance.bIsSet = SN.bHasIsExposedInstance;
			if (SN.bHasIsExposedInstance)
			{
				SN.HasIsExposedInstance.bValue = SN.bIsExposedInstance;
			}
		}
	}
}

const FDesign2UMGFigmaWidgetMetadataRecord* UDesign2UMGFigmaMetadataClassExtension::FindRecordBySourceNodeId(
	const FString& SourceNodeId) const
{
	return WidgetRecords.FindByPredicate(
		[&SourceNodeId](const FDesign2UMGFigmaWidgetMetadataRecord& Record)
		{
			return Record.SourceNode.SourceNodeId == SourceNodeId;
		});
}

const FDesign2UMGFigmaWidgetMetadataRecord* UDesign2UMGFigmaMetadataClassExtension::FindRecordByWidgetName(
	const FName WidgetName) const
{
	return WidgetRecords.FindByPredicate(
		[WidgetName](const FDesign2UMGFigmaWidgetMetadataRecord& Record)
		{
			return Record.WidgetName == WidgetName;
		});
}

#if WITH_EDITOR
bool UDesign2UMGFigmaMetadataClassExtension::VerifyBindings(const UWidgetTree* WidgetTree) const
{
	if (WidgetRecords.Num() == 0)
	{
		return true;
	}

	if (!WidgetTree)
	{
		return false;
	}

	for (const FDesign2UMGFigmaWidgetMetadataRecord& Record : WidgetRecords)
	{
		if (!Record.IsValid() || !WidgetTree->FindWidget(Record.WidgetName))
		{
			return false;
		}
	}

	return true;
}
#endif

void UDesign2UMGFigmaMetadataBlueprintExtension::SetMetadata(
	const FUMGSourceDocumentData& InDocumentData,
	const TArray<FDesign2UMGFigmaWidgetMetadataRecord>& InWidgetRecords)
{
	DocumentData = InDocumentData;
	WidgetRecords.Reset();
	for (const FDesign2UMGFigmaWidgetMetadataRecord& Record : InWidgetRecords)
	{
		if (Record.IsValid())
		{
			WidgetRecords.Add(Record);
		}
	}
}

void UDesign2UMGFigmaMetadataBlueprintExtension::HandleBeginCompilation(
	FWidgetBlueprintCompilerContext& InCreationContext)
{
	Super::HandleBeginCompilation(InCreationContext);
	CompilerContext = &InCreationContext;
}

void UDesign2UMGFigmaMetadataBlueprintExtension::HandleFinishCompilingClass(
	UWidgetBlueprintGeneratedClass* Class)
{
	Super::HandleFinishCompilingClass(Class);

	if (!CompilerContext || !HasAnyMetadata() || !Class)
	{
		return;
	}

	UDesign2UMGFigmaMetadataClassExtension* NewExtension =
		NewObject<UDesign2UMGFigmaMetadataClassExtension>(Class);
	NewExtension->SetMetadata(DocumentData, WidgetRecords);
	CompilerContext->AddExtension(Class, NewExtension);
}

bool UDesign2UMGFigmaMetadataBlueprintExtension::HandleValidateGeneratedClass(
	UWidgetBlueprintGeneratedClass* Class)
{
	if (!HasAnyMetadata() || !Class)
	{
		return true;
	}

	const UDesign2UMGFigmaMetadataClassExtension* RuntimeExtension =
		Class->GetExtension<UDesign2UMGFigmaMetadataClassExtension>();
	return RuntimeExtension ? RuntimeExtension->VerifyBindings(Class->GetWidgetTreeArchetype()) : false;
}

void UDesign2UMGFigmaMetadataBlueprintExtension::HandleEndCompilation()
{
	CompilerContext = nullptr;
}

bool UDesign2UMGFigmaMetadataBlueprintExtension::HasAnyMetadata() const
{
	return DocumentData.HasAnyComponentCatalog() || WidgetRecords.Num() > 0;
}
