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

	inline bool TryGetString(const TSharedPtr<FJsonObject>& Object, const FString& Key, FString& OutValue)
	{
		return Object && Object->TryGetStringField(Key, OutValue);
	}

	inline bool TryGetBool(const TSharedPtr<FJsonObject>& Object, const FString& Key, bool& OutValue)
	{
		return Object && Object->TryGetBoolField(Key, OutValue);
	}

	inline bool TryGetObject(const TSharedPtr<FJsonObject>& Object, const FString& Key, const TSharedPtr<FJsonObject>*& OutValue)
	{
		return Object && Object->TryGetObjectField(Key, OutValue);
	}

	// Numeric existence reads delegate to the FJsonObject overloads so conversion
	// semantics (float narrowing, int32 range check + round-half-from-zero) stay
	// engine-defined instead of being re-implemented here.
	inline bool TryGetFloat(const TSharedPtr<FJsonObject>& Object, const FString& Key, float& OutValue)
	{
		return Object && Object->TryGetNumberField(Key, OutValue);
	}

	inline bool TryGetInt32(const TSharedPtr<FJsonObject>& Object, const FString& Key, int32& OutValue)
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
	 * Serialize JSON with indented, spaced formatting for parser metadata.
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
 * Serialize JSON without whitespace for prototype step payloads.
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
