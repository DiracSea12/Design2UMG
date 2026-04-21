// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGSourceMetadata.h"
#include "DesignSchemaSourceExtensions.h"
#include "UMGSchema.h"

namespace
{
	const FString* FindStructuredSourceExtension(const TSharedPtr<FDesignNode>& Node, const FString& Key)
	{
		if (!Node.IsValid())
		{
			return nullptr;
		}

		if (Key == TEXT("sourceNodeType"))
		{
			return &Node->Source().SourceNodeType;
		}

		return nullptr;
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

	bool HasMeaningfulSourceMetadata(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key)
	{
		const FString Value = GetSourceExtension(Node, Key).TrimStartAndEnd();
		return !Value.IsEmpty()
			&& !Value.Equals(TEXT("[]"), ESearchCase::CaseSensitive)
			&& !Value.Equals(TEXT("{}"), ESearchCase::CaseSensitive)
			&& !Value.Equals(TEXT("null"), ESearchCase::IgnoreCase);
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
		Result.SourceNodeType = GetSourceExtension(Node, TEXT("sourceNodeType"));
		Result.ComponentId = Node->Source().ComponentInstance.ComponentId;
		Result.ComponentSetId = Node->Source().ComponentInstance.ComponentSetId;
		Result.MainComponentKey = Node->Source().ComponentInstance.MainComponentKey;
		Result.ComponentPropertiesJson = Node->Source().ComponentInstance.ComponentPropertiesJson;
		Result.VariantPropertiesJson = Node->Source().ComponentInstance.VariantPropertiesJson;
		Result.OverridesJson = Node->Source().ComponentInstance.OverridesJson;
		Result.bIsExposedInstance = Node->Source().ComponentInstance.bIsExposedInstance;
		Result.bHasIsExposedInstance = Node->Source().ComponentInstance.bHasIsExposedInstance;

		if (Result.IsComponentDefinition() && Result.ComponentId.IsEmpty())
		{
			Result.ComponentId = Node->Id;
		}
		if (Result.IsComponentSet() && Result.ComponentSetId.IsEmpty())
		{
			Result.ComponentSetId = Node->Id;
		}

		return Result;
	}

	bool IsSourceNodeType(const TSharedPtr<FDesignNode>& Node, const TCHAR* ExpectedType)
	{
		return GetSourceExtension(Node, TEXT("sourceNodeType")).Equals(ExpectedType, ESearchCase::IgnoreCase);
	}

	bool ShouldUseRenderFallback(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		const FString Scope = Node->Source().RenderFallback.Scope;
		const bool bReplaceSubtree = Scope.Equals(TEXT("subtree"), ESearchCase::IgnoreCase);
		if (Node->Children.Num() != 0 && !bReplaceSubtree)
		{
			return false;
		}

		if (Node->Type == EDesignNodeType::Text
			|| Node->Type == EDesignNodeType::IconFont
			|| Node->Type == EDesignNodeType::Ref)
		{
			return false;
		}

		const FString& TexturePath = Node->Source().RenderFallback.ImageRef;
		if (TexturePath.IsEmpty())
		{
			return false;
		}

		if (!Scope.IsEmpty()
			&& !Scope.Equals(TEXT("full"), ESearchCase::IgnoreCase)
			&& !bReplaceSubtree)
		{
			return false;
		}

		const FString SourceNodeType = GetSourceExtension(Node, TEXT("sourceNodeType"));
		if (SourceNodeType.Equals(TEXT("INSTANCE"), ESearchCase::IgnoreCase))
		{
			return false;
		}

		if (Node->Source().Prototype.Interactions.Num() > 0 || HasMeaningfulSourceMetadata(Node, TEXT("interactions")))
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

		float WidthOverride = Node->Transform.Width.Value;
		const FString& FallbackWidth = Node->Source().RenderFallback.Width;
		if (!FallbackWidth.IsEmpty())
		{
			WidthOverride = FCString::Atof(*FallbackWidth);
		}

		float HeightOverride = Node->Transform.Height.Value;
		const FString& FallbackHeight = Node->Source().RenderFallback.Height;
		if (!FallbackHeight.IsEmpty())
		{
			HeightOverride = FCString::Atof(*FallbackHeight);
		}

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

		return Node->Source().LayoutExtensions.bHasCounterAxisSpacing
			? Node->Source().LayoutExtensions.CounterAxisSpacing
			: Node->LayoutData.Gap;
	}
}
