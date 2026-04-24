// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGSourceMetadata.h"
#include "DesignSchemaSourceExtensions.h"
#include "UMGSchema.h"

namespace
{
	namespace SourceMetadataKeys
	{
		const TCHAR* SourceNodeType = TEXT("sourceNodeType");
		const TCHAR* Interactions = TEXT("interactions");
		const TCHAR* RenderFallbackScopeFull = TEXT("full");
		const TCHAR* RenderFallbackScopeSubtree = TEXT("subtree");
		const TCHAR* SourceNodeTypeInstance = TEXT("INSTANCE");
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

		bool HasMeaningfulSourceMetadataValue(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key)
		{
			const FString Value = GetSourceExtensionValue(Node, Key).TrimStartAndEnd();
			return !Value.IsEmpty()
				&& !Value.Equals(TEXT("[]"), ESearchCase::CaseSensitive)
				&& !Value.Equals(TEXT("{}"), ESearchCase::CaseSensitive)
				&& !Value.Equals(TEXT("null"), ESearchCase::IgnoreCase);
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
			Target.bIsExposedInstance = Source.bIsExposedInstance;
			Target.bHasIsExposedInstance = Source.bHasIsExposedInstance;
		}

		void BackfillDefinitionIds(FUMGSourceNodeData& SourceData, const TSharedPtr<FDesignNode>& Node)
		{
			if (SourceData.IsComponentDefinition() && SourceData.ComponentId.IsEmpty())
			{
				SourceData.ComponentId = Node->Id;
			}
			if (SourceData.IsComponentSet() && SourceData.ComponentSetId.IsEmpty())
			{
				SourceData.ComponentSetId = Node->Id;
			}
		}
	}

	namespace RenderFallback
	{
		bool ReplacesSubtree(const TSharedPtr<FDesignNode>& Node)
		{
			return Node.IsValid()
				&& Node->Source().RenderFallback.Scope.Equals(SourceMetadataKeys::RenderFallbackScopeSubtree, ESearchCase::IgnoreCase);
		}

		bool HasSupportedScope(const TSharedPtr<FDesignNode>& Node)
		{
			const FString& Scope = Node->Source().RenderFallback.Scope;
			return Scope.IsEmpty()
				|| Scope.Equals(SourceMetadataKeys::RenderFallbackScopeFull, ESearchCase::IgnoreCase)
				|| ReplacesSubtree(Node);
		}

		bool SupportsLeafReplacement(const TSharedPtr<FDesignNode>& Node)
		{
			return Node->Children.Num() == 0 || ReplacesSubtree(Node);
		}

		bool IsUnsupportedNodeType(const TSharedPtr<FDesignNode>& Node)
		{
			return Node->Type == EDesignNodeType::Text
				|| Node->Type == EDesignNodeType::IconFont
				|| Node->Type == EDesignNodeType::Ref;
		}

		bool HasImageRef(const TSharedPtr<FDesignNode>& Node)
		{
			return Node.IsValid() && !Node->Source().RenderFallback.ImageRef.IsEmpty();
		}

		bool IsSourceInstanceNode(const TSharedPtr<FDesignNode>& Node)
		{
			return MetadataAccess::GetSourceExtensionValue(Node, SourceMetadataKeys::SourceNodeType)
				.Equals(SourceMetadataKeys::SourceNodeTypeInstance, ESearchCase::IgnoreCase);
		}

		bool HasPrototypeInteractions(const TSharedPtr<FDesignNode>& Node)
		{
			return Node.IsValid()
				&& (Node->Source().Prototype.Interactions.Num() > 0
					|| MetadataAccess::HasMeaningfulSourceMetadataValue(Node, SourceMetadataKeys::Interactions));
		}

		float ResolveDimensionOverride(const FString& OverrideValue, float DefaultValue)
		{
			return OverrideValue.IsEmpty() ? DefaultValue : FCString::Atof(*OverrideValue);
		}
	}

	namespace LayoutMetadata
	{
		float ResolveWrapCrossAxisGapValue(const TSharedPtr<FDesignNode>& Node)
		{
			return Node->Source().LayoutExtensions.bHasCounterAxisSpacing
				? Node->Source().LayoutExtensions.CounterAxisSpacing
				: Node->LayoutData.Gap;
		}
	}
}

namespace Design2UMGSourceMetadata
{
	bool HasNodeMetadataFlag(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString* Value = Node->Metadata.Find(Key);
		return Value && Value->Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	FString GetSourceExtension(const TSharedPtr<FDesignNode>& Node, const FString& Key)
	{
		return MetadataAccess::GetSourceExtensionValue(Node, Key);
	}

	bool HasMeaningfulSourceMetadata(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key)
	{
		return MetadataAccess::HasMeaningfulSourceMetadataValue(Node, Key);
	}

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

	bool ShouldUseRenderFallback(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (!RenderFallback::SupportsLeafReplacement(Node))
		{
			return false;
		}

		if (RenderFallback::IsUnsupportedNodeType(Node))
		{
			return false;
		}

		if (!RenderFallback::HasImageRef(Node))
		{
			return false;
		}

		if (!RenderFallback::HasSupportedScope(Node))
		{
			return false;
		}

		if (RenderFallback::IsSourceInstanceNode(Node))
		{
			return false;
		}

		if (RenderFallback::HasPrototypeInteractions(Node))
		{
			return false;
		}

		return true;
	}

	FUMGWidgetNode CreateRenderFallbackWidget(const TSharedPtr<FDesignNode>& Node)
	{
		FUMGWidgetNode Widget;
		Widget.Type = TEXT("Image");

		const FString& TexturePath = Node->Source().RenderFallback.ImageRef;
		Widget.Brush.TexturePath = TexturePath;
		Widget.Brush.Image.TexturePath = TexturePath;
		Widget.Brush.Image.bHasTexturePath = true;

		const float WidthOverride = RenderFallback::ResolveDimensionOverride(
			Node->Source().RenderFallback.Width,
			Node->Transform.Width.Value);
		const float HeightOverride = RenderFallback::ResolveDimensionOverride(
			Node->Source().RenderFallback.Height,
			Node->Transform.Height.Value);

		if (WidthOverride > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = WidthOverride;
		}
		if (HeightOverride > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = HeightOverride;
		}

		return Widget;
	}

	float ResolveWrapCrossAxisGap(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return 0.0f;
		}

		return LayoutMetadata::ResolveWrapCrossAxisGapValue(Node);
	}
}
