// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace DesignJsonUtils
{
	inline FString GetString(const TSharedPtr<FJsonObject>& Object, const FString& Key, const FString& DefaultValue = FString())
	{
		FString Value;
		return (Object && Object->TryGetStringField(Key, Value)) ? Value : DefaultValue;
	}

	inline double GetNumber(const TSharedPtr<FJsonObject>& Object, const FString& Key, double DefaultValue = 0.0)
	{
		double Value = 0.0;
		return (Object && Object->TryGetNumberField(Key, Value)) ? Value : DefaultValue;
	}

	inline bool TryGetNumber(const TSharedPtr<FJsonObject>& Object, const FString& Key, double& OutValue)
	{
		return Object && Object->TryGetNumberField(Key, OutValue);
	}

	inline bool GetBool(const TSharedPtr<FJsonObject>& Object, const FString& Key, bool DefaultValue = true)
	{
		bool Value = false;
		return (Object && Object->TryGetBoolField(Key, Value)) ? Value : DefaultValue;
	}

	inline TSharedPtr<FJsonObject> GetObject(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		const TSharedPtr<FJsonObject>* SubObject = nullptr;
		if (Object && Object->TryGetObjectField(Key, SubObject))
		{
			return *SubObject;
		}
		return nullptr;
	}

	inline const TArray<TSharedPtr<FJsonValue>>* GetArray(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Object && Object->TryGetArrayField(Key, Array))
		{
			return Array;
		}
		return nullptr;
	}

	inline TSharedPtr<FJsonValue> TryGetField(const TSharedPtr<FJsonObject>& Object, const FString& Key)
	{
		return Object ? Object->TryGetField(Key) : nullptr;
	}

	/**
	 * Serialize a JSON value/object to a pretty-printed multi-line string (TJsonWriter<>
	 * defaults to TPrettyJsonPrintPolicy: indented, spaced). Historical consumers are the
	 * Figma/Pen parser metadata fields; their output has always been pretty-printed.
	 */
	inline FString ToPrettyString(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}

		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		if (!FJsonSerializer::Serialize(Value.ToSharedRef(), FString(), Writer))
		{
			return FString();
		}
		return Output;
	}

	inline FString ToPrettyString(const TSharedPtr<FJsonObject>& Object)
	{
		return Object.IsValid()
			? ToPrettyString(MakeShared<FJsonValueObject>(Object.ToSharedRef()))
			: FString();
	}

/**
 * Serialize a JSON value/object to a condensed single-line string (no indentation, no
 * spaces) via TCondensedJsonPrintPolicy. Historical consumers are the prototype step
 * graph fields (EasingJson/TransitionJson/VariableValueJson/ConditionJson);
 * their output has always been condensed.
 */
	inline FString ToCondensedString(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}

		FString Json;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		if (FJsonSerializer::Serialize(Value.ToSharedRef(), FString(), Writer))
		{
			return Json;
		}

		return FString();
	}

	inline FString ToCondensedString(const TSharedPtr<FJsonObject>& Object)
	{
		return Object.IsValid()
			? ToCondensedString(MakeShared<FJsonValueObject>(Object.ToSharedRef()))
			: FString();
	}

	inline bool TryParseObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
	{
		OutObject.Reset();
		if (Json.IsEmpty())
		{
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	inline bool TryParseValue(const FString& Json, TSharedPtr<FJsonValue>& OutValue)
	{
		OutValue.Reset();
		if (Json.IsEmpty())
		{
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutValue) && OutValue.IsValid();
	}

	inline bool TryParseArray(const FString& Json, TArray<TSharedPtr<FJsonValue>>& OutArray)
	{
		OutArray.Reset();
		if (Json.IsEmpty())
		{
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutArray);
	}
}
