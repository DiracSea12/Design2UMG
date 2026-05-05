#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
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

	inline FString ToCompactString(const TSharedPtr<FJsonValue>& Value)
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
}
