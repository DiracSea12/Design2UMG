// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGSourceMetadata.h"
#include "DesignSchemaSourceExtensions.h"
#include "UMGSchema.h"

namespace
{
	namespace SourceMetadataKeys
	{
		const TCHAR* SourceNodeType = TEXT("sourceNodeType");
	}

	namespace MetadataAccess
	{
		const FString* FindStructuredSourceExtension(const TSharedPtr<FDesignNode>& Node, const FString& Key)
		{
			if (!Node.IsValid())
			{
				return nullptr;
			}

			if (Key == SourceMetadataKeys::SourceNodeType)
			{
				return &Node->Source().SourceNodeType;
			}

			return nullptr;
		}

		FString GetSourceExtensionValue(const TSharedPtr<FDesignNode>& Node, const FString& Key)
		{
			if (!Node.IsValid())
			{
				return FString();
			}

			if (const FString* StructuredValue = FindStructuredSourceExtension(Node, Key))
			{
				return *StructuredValue;
			}
			if (const FString* Value = Node->Metadata.Find(Key))
			{
				return *Value;
			}
			return FString();
		}

	}

	namespace SourceNodeDataBuilder
	{
		void CopyComponentInstanceData(FUMGSourceNodeData& Target, const FDesignComponentInstanceData& Source)
		{
			Target.ComponentId = Source.ComponentId;
			Target.ComponentSetId = Source.ComponentSetId;
			Target.MainComponentKey = Source.MainComponentKey;
			Target.ComponentPropertiesJson = Source.ComponentPropertiesJson;
			Target.VariantPropertiesJson = Source.VariantPropertiesJson;
			Target.OverridesJson = Source.OverridesJson;
			Target.HasIsExposedInstance = Source.IsExposedInstance;
		}

		void BackfillDefinitionIds(FUMGSourceNodeData& SourceData, const TSharedPtr<FDesignNode>& Node)
		{
			if (SourceData.IsFigmaComponentDefinition() && SourceData.ComponentId.IsEmpty())
			{
				SourceData.ComponentId = Node->Id;
			}
			if (SourceData.IsFigmaComponentSet() && SourceData.ComponentSetId.IsEmpty())
			{
				SourceData.ComponentSetId = Node->Id;
			}
		}
	}

}

namespace Design2UMGSourceMetadata
{
	FUMGSourceNodeData BuildSourceNodeData(const TSharedPtr<FDesignNode>& Node)
	{
		FUMGSourceNodeData Result;
		if (!Node.IsValid())
		{
			return Result;
		}

		Result.SourceNodeId = Node->Id;
		Result.SourceNodeName = Node->Name;
		Result.SourceNodeType = MetadataAccess::GetSourceExtensionValue(Node, SourceMetadataKeys::SourceNodeType);
		SourceNodeDataBuilder::CopyComponentInstanceData(Result, Node->Source().ComponentInstance);
		SourceNodeDataBuilder::BackfillDefinitionIds(Result, Node);

		return Result;
	}

	bool IsSourceNodeType(const TSharedPtr<FDesignNode>& Node, const TCHAR* ExpectedType)
	{
		return MetadataAccess::GetSourceExtensionValue(Node, SourceMetadataKeys::SourceNodeType)
			.Equals(ExpectedType, ESearchCase::IgnoreCase);
	}

	bool IsFigmaTextPathNode(const TSharedPtr<FDesignNode>& Node)
	{
		return IsSourceNodeType(Node, TEXT("TEXT_PATH"));
	}

}
