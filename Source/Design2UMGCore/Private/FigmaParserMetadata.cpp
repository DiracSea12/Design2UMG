// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserMetadata.h"

#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "FigmaParserInternal.h"
#include "ParserSharedSemantics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	using DesignJsonUtils::GetString;
	using DesignJsonUtils::TryGetNumber;

	void AddExtensionIfNonEmpty(TMap<FString, FString>& Extensions, const FString& Key, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Extensions.Add(Key, Value);
		}
	}

	void AddNumericExtensionIfPresent(
		const TSharedPtr<FJsonObject>& Obj,
		TMap<FString, FString>& Extensions,
		const FString& SourceKey,
		const FString& ExtKey)
	{
		double Value = 0.0;
		if (TryGetNumber(Obj, SourceKey, Value))
		{
			Extensions.Add(ExtKey, FString::SanitizeFloat(static_cast<float>(Value)));
		}
	}

	bool IsSemanticFieldName(const FString& FieldName)
	{
		return FieldName.Equals(TEXT("placeholder"), ESearchCase::IgnoreCase)
			|| FieldName.Equals(TEXT("slot"), ESearchCase::IgnoreCase);
	}

	bool IsExplicitFalseString(const FString& InValue)
	{
		const FString Value = InValue.TrimStartAndEnd().ToLower();
		return Value.IsEmpty()
			|| Value == TEXT("false")
			|| Value == TEXT("0")
			|| Value == TEXT("no")
			|| Value == TEXT("off")
			|| Value == TEXT("none")
			|| Value == TEXT("null");
	}

	bool ReadSemanticSignalField(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		switch (Value->Type)
		{
		case EJson::Boolean:
			return Value->AsBool();
		case EJson::Number:
			return !FMath::IsNearlyZero(static_cast<float>(Value->AsNumber()));
		case EJson::String:
			return !IsExplicitFalseString(Value->AsString());
		case EJson::Array:
		case EJson::Object:
			return true;
		default:
			return false;
		}
	}

	// Depth cap for scanning untrusted pluginData/sharedPluginData JSON for semantic
	// hints. This recursion runs outside the parser's FDesignParserBudget, so it carries
	// its own bound, styled after DesignTreeLimits' named limits; real pluginData values
	// nest only a handful of levels, so 32 is a generous fixed ceiling, not a measured
	// optimum.
	constexpr int32 MaxPluginDataHintScanDepth = 32;

	bool TryReadSemanticHintFromJsonValue(const TSharedPtr<FJsonValue>& Value, int32 Depth = 0)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Depth >= MaxPluginDataHintScanDepth)
		{
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("FigmaParserMetadata: pluginData semantic-hint scan stopped at depth cap (%d)"),
				MaxPluginDataHintScanDepth);
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> ObjectValue = Value->AsObject();
			if (!ObjectValue.IsValid())
			{
				return false;
			}

			for (const auto& Pair : ObjectValue->Values)
			{
				if (IsSemanticFieldName(FString(*Pair.Key)) && ReadSemanticSignalField(Pair.Value))
				{
					return true;
				}
				if (TryReadSemanticHintFromJsonValue(Pair.Value, Depth + 1))
				{
					return true;
				}
			}
			return false;
		}

		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& ChildValue : Value->AsArray())
			{
				if (TryReadSemanticHintFromJsonValue(ChildValue, Depth + 1))
				{
					return true;
				}
			}
			return false;
		}

		if (Value->Type == EJson::String)
		{
			const FString StringValue = Value->AsString().TrimStartAndEnd();
			if (StringValue.IsEmpty())
			{
				return false;
			}

			TSharedPtr<FJsonObject> JsonObject;
			if (DesignJsonUtils::TryParseObject(StringValue, JsonObject))
			{
				return TryReadSemanticHintFromJsonValue(MakeShared<FJsonValueObject>(JsonObject), Depth + 1);
			}

			return IsSemanticFieldName(StringValue);
		}

		return false;
	}
}

namespace FigmaParserMetadata
{
	void CaptureRenderFallbackMetadata(const TSharedPtr<FJsonObject>& Obj, FDesignNode& OutNode)
	{
		if (!Obj.IsValid())
		{
			return;
		}

		const FString ImageRef = GetString(Obj, TEXT("design2umgRenderFallbackImageRef"));
		if (!ImageRef.IsEmpty())
		{
			OutNode.Visual.RenderFallback.ImageRef = ImageRef;
		}

		const FString Scope = GetString(Obj, TEXT("design2umgRenderFallbackScope"));
		if (Scope.Equals(TEXT("subtree"), ESearchCase::IgnoreCase))
		{
			OutNode.Visual.RenderFallback.Scope = EDesignRenderFallbackScope::Subtree;
		}
		else if (!Scope.IsEmpty() && !Scope.Equals(TEXT("full"), ESearchCase::IgnoreCase))
		{
			OutNode.Visual.RenderFallback.Scope = EDesignRenderFallbackScope::Unsupported;
		}

		for (const TCHAR* Key : {
			 TEXT("design2umgRenderFallbackImageRef"),
			 TEXT("design2umgRenderFallbackScope"),
			 TEXT("design2umgRenderFallbackSource"),
			 TEXT("design2umgRenderFallbackReason") })
		{
			const FString Value = GetString(Obj, Key);
			if (!Value.IsEmpty())
			{
				OutNode.Metadata.Add(Key, Value);
			}
		}

		OutNode.Visual.RenderFallback.bSuppressedBySourceSemantics =
			OutNode.Source().ComponentInstance.bIsInstance
			|| OutNode.Source().Prototype.Interactions.Num() > 0;
	}

	void CaptureSharedMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
	{
		if (!Obj.IsValid())
		{
			return;
		}

		for (const TCHAR* Key : { TEXT("overflowDirection"), TEXT("prototypeStartNodeID"), TEXT("transitionNodeID") })
		{
			AddExtensionIfNonEmpty(Extensions, Key, GetString(Obj, Key));
		}

		for (const TCHAR* Key : {
			 TEXT("layoutGrids"), TEXT("interactions"), TEXT("flowStartingPoints"), TEXT("prototypeDevice"),
			 TEXT("componentPropertyReferences"), TEXT("explicitVariableModes"), TEXT("resolvedVariableModes"),
			 TEXT("componentPropertyDefinitions"), TEXT("variantGroupProperties"), TEXT("annotations"), TEXT("measurements"),
			 TEXT("exportSettings"), TEXT("devStatus"), TEXT("branchData") })
		{
			FigmaParserInternal::AddJsonExtensionIfPresent(Obj, Extensions, Key, Key);
		}
	}

	void CaptureLayoutMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
	{
		if (!Obj.IsValid())
		{
			return;
		}

		for (const TCHAR* Key : {
			 TEXT("counterAxisAlignContent"), TEXT("layoutPositioning"), TEXT("layoutWrap"), TEXT("layoutAlign"),
			 TEXT("primaryAxisSizingMode"), TEXT("counterAxisSizingMode"), TEXT("gridRowsSizing"), TEXT("gridColumnsSizing"),
			 TEXT("gridChildHorizontalAlign"), TEXT("gridChildVerticalAlign"), TEXT("mainComponentKey"), TEXT("componentSetId"), TEXT("componentSetKey") })
		{
			AddExtensionIfNonEmpty(Extensions, Key, GetString(Obj, Key));
		}

		for (const TCHAR* Key : {
			 TEXT("counterAxisSpacing"), TEXT("minWidth"), TEXT("maxWidth"), TEXT("minHeight"),
			 TEXT("maxHeight"), TEXT("gridRowCount"), TEXT("gridColumnCount"), TEXT("gridRowGap"), TEXT("gridColumnGap"),
			 TEXT("gridRowSpan"), TEXT("gridColumnSpan"), TEXT("gridColumnAnchorIndex"), TEXT("gridRowAnchorIndex") })
		{
			AddNumericExtensionIfPresent(Obj, Extensions, Key, Key);
		}

		for (const TCHAR* Key : { TEXT("componentProperties"), TEXT("variantProperties"), TEXT("overrides"), TEXT("exposedInstances") })
		{
			FigmaParserInternal::AddJsonExtensionIfPresent(Obj, Extensions, Key, Key);
		}

		CaptureSharedMetadataExtensions(Obj, Extensions);
	}

	void ApplyFigmaSemanticHints(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& Node)
	{
		if (!NodeObj.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonValue> PlaceholderField = NodeObj->TryGetField(TEXT("placeholder"));
		const TSharedPtr<FJsonValue> SlotField = NodeObj->TryGetField(TEXT("slot"));

		if (SlotField.IsValid() && SlotField->Type == EJson::Array)
		{
			ParserSharedSemantics::AddSourceSlotMetadata(Node, SlotField->AsArray().Num());
		}

		const bool bExplicitPlaceholder = ReadSemanticSignalField(PlaceholderField);
		const bool bExplicitSlot = ReadSemanticSignalField(SlotField);
		const bool bHasPluginSemanticMarker =
			TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("pluginData")))
			|| TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("sharedPluginData")));

		// Placeholder surface is the main runtime contract for this family; present/count stay preservation-only metadata.
		Node.LayoutData.bSemanticPlaceholderSurface =
			bExplicitPlaceholder
			|| bExplicitSlot
			|| bHasPluginSemanticMarker;
	}
}
