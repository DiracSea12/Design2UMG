// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserMetadata.h"

#include "DesignJsonUtils.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	using DesignJsonUtils::GetString;
	using DesignJsonUtils::ToCompactString;
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

	void AddJsonExtensionIfPresent(
		const TSharedPtr<FJsonObject>& Obj,
		TMap<FString, FString>& Extensions,
		const FString& SourceKey,
		const FString& ExtKey)
	{
		if (!Obj.IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(SourceKey);
		if (FieldValue.IsValid())
		{
			Extensions.Add(ExtKey, ToCompactString(FieldValue));
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

	bool TryReadSemanticHintFromJsonValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> ObjectValue = Value->AsObject();
			if (!ObjectValue.IsValid())
			{
				return false;
			}

			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ObjectValue->Values)
			{
				if (IsSemanticFieldName(Pair.Key) && ReadSemanticSignalField(Pair.Value))
				{
					return true;
				}
				if (TryReadSemanticHintFromJsonValue(Pair.Value))
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
				if (TryReadSemanticHintFromJsonValue(ChildValue))
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
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StringValue);
			if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
			{
				return TryReadSemanticHintFromJsonValue(MakeShared<FJsonValueObject>(JsonObject));
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

		const struct
		{
			const TCHAR* SourceKey;
			FString FDesignRenderFallbackData::* TargetField;
		}
		RenderFallbackKeys[] = {
			{ TEXT("design2umgRenderFallbackImageRef"), &FDesignRenderFallbackData::ImageRef },
			{ TEXT("design2umgRenderFallbackScope"), &FDesignRenderFallbackData::Scope },
			{ TEXT("design2umgRenderFallbackSource"), &FDesignRenderFallbackData::Source },
			{ TEXT("design2umgRenderFallbackReason"), &FDesignRenderFallbackData::Reason },
			{ TEXT("design2umgRenderFallbackOffsetX"), &FDesignRenderFallbackData::OffsetX },
			{ TEXT("design2umgRenderFallbackOffsetY"), &FDesignRenderFallbackData::OffsetY },
			{ TEXT("design2umgRenderFallbackWidth"), &FDesignRenderFallbackData::Width },
			{ TEXT("design2umgRenderFallbackHeight"), &FDesignRenderFallbackData::Height },
		};

		for (const auto& KeyPair : RenderFallbackKeys)
		{
			const FString Value = GetString(Obj, FString(KeyPair.SourceKey));
			if (!Value.IsEmpty())
			{
				OutNode.MutableSource().RenderFallback.*(KeyPair.TargetField) = Value;
			}
		}
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
			AddJsonExtensionIfPresent(Obj, Extensions, Key, Key);
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
			 TEXT("gridChildHorizontalAlign"), TEXT("gridChildVerticalAlign"), TEXT("mainComponentKey"), TEXT("componentSetId") })
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
			AddJsonExtensionIfPresent(Obj, Extensions, Key, Key);
		}

		CaptureSharedMetadataExtensions(Obj, Extensions);
	}

	void ApplyFigmaSemanticHints(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& Node)
	{
		const TSharedPtr<FJsonValue> PlaceholderField = NodeObj->TryGetField(TEXT("placeholder"));
		const TSharedPtr<FJsonValue> SlotField = NodeObj->TryGetField(TEXT("slot"));

		if (SlotField.IsValid() && SlotField->Type == EJson::Array)
		{
			const int32 SlotCount = SlotField->AsArray().Num();
			Node.Metadata.Add(TEXT("source.slot.present"), TEXT("true"));
			Node.Metadata.Add(TEXT("source.slot.count"), FString::FromInt(SlotCount));
			if (SlotCount == 0)
			{
				Node.Metadata.Add(TEXT("source.slot.empty"), TEXT("true"));
			}
		}

		const bool bExplicitPlaceholder = ReadSemanticSignalField(PlaceholderField);
		const bool bExplicitSlot = ReadSemanticSignalField(SlotField);
		const bool bHasPluginSemanticMarker =
			TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("pluginData")))
			|| TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("sharedPluginData")));

		Node.LayoutData.bSemanticPlaceholderSurface =
			bExplicitPlaceholder
			|| bExplicitSlot
			|| bHasPluginSemanticMarker;
	}
}
