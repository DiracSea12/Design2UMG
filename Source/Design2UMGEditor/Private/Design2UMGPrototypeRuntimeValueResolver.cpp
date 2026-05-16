// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeRuntimeValueResolver.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	enum class EDesign2UMGPrototypeRuntimeValueKind : uint8
	{
		None, Boolean, Number, String, Color
	};

	enum class EDesign2UMGPrototypeSerializedValueDomain : uint8
	{
		Unknown,
		Boolean,
		Number,
		String,
		Color,
		VariableAlias,
		Expression
	};

	enum class EDesign2UMGPrototypeExpressionFunction : uint8
	{
		Unknown,
		And,
		Or,
		Not,
		Equals,
		NotEquals,
		GreaterThan,
		GreaterThanOrEqual,
		LessThan,
		LessThanOrEqual,
		VarModeLookup
	};

	struct FDesign2UMGPrototypeRuntimeValue
	{
		EDesign2UMGPrototypeRuntimeValueKind Kind = EDesign2UMGPrototypeRuntimeValueKind::None;
		bool BoolValue = false;
		double NumberValue = 0.0;
		FString StringValue;
		FLinearColor ColorValue = FLinearColor::Transparent;
	};

	EDesign2UMGPrototypeSerializedValueDomain ParseSerializedValueDomain(const FString& DomainToken)
	{
		if (DomainToken.Equals(TEXT("BOOLEAN"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeSerializedValueDomain::Boolean;
		}
		if (DomainToken.Equals(TEXT("FLOAT"), ESearchCase::IgnoreCase)
			|| DomainToken.Equals(TEXT("NUMBER"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeSerializedValueDomain::Number;
		}
		if (DomainToken.Equals(TEXT("STRING"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeSerializedValueDomain::String;
		}
		if (DomainToken.Equals(TEXT("COLOR"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeSerializedValueDomain::Color;
		}
		if (DomainToken.Equals(TEXT("VARIABLE_ALIAS"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeSerializedValueDomain::VariableAlias;
		}
		if (DomainToken.Equals(TEXT("EXPRESSION"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeSerializedValueDomain::Expression;
		}
		return EDesign2UMGPrototypeSerializedValueDomain::Unknown;
	}

	EDesign2UMGPrototypeExpressionFunction ParseExpressionFunctionToken(const FString& FunctionToken)
	{
		if (FunctionToken.Equals(TEXT("AND"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::And;
		}
		if (FunctionToken.Equals(TEXT("OR"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::Or;
		}
		if (FunctionToken.Equals(TEXT("NOT"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::Not;
		}
		if (FunctionToken.Equals(TEXT("EQUALS"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::Equals;
		}
		if (FunctionToken.Equals(TEXT("NOT_EQUALS"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::NotEquals;
		}
		if (FunctionToken.Equals(TEXT("GREATER_THAN"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::GreaterThan;
		}
		if (FunctionToken.Equals(TEXT("GREATER_THAN_OR_EQUAL"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::GreaterThanOrEqual;
		}
		if (FunctionToken.Equals(TEXT("LESS_THAN"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::LessThan;
		}
		if (FunctionToken.Equals(TEXT("LESS_THAN_OR_EQUAL"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::LessThanOrEqual;
		}
		if (FunctionToken.Equals(TEXT("VAR_MODE_LOOKUP"), ESearchCase::IgnoreCase))
		{
			return EDesign2UMGPrototypeExpressionFunction::VarModeLookup;
		}
		return EDesign2UMGPrototypeExpressionFunction::Unknown;
	}

	bool TryParseJsonValue(const FString& Json, TSharedPtr<FJsonValue>& OutValue)
	{
		OutValue.Reset();
		if (Json.IsEmpty())
		{
			return false;
		}
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, OutValue) && OutValue.IsValid();
	}

	FString GetJsonString(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return FString();
		}
		FString Value;
		if (Object->TryGetStringField(FieldName, Value))
		{
			return Value;
		}
		return FString();
	}

	TSharedPtr<FJsonObject> GetJsonObject(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}
		const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
		if (Object->TryGetObjectField(FieldName, ObjectPtr))
		{
			return *ObjectPtr;
		}
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetJsonArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
	{
		if (!Object.IsValid())
		{
			return nullptr;
		}
		const TArray<TSharedPtr<FJsonValue>>* ArrayPtr = nullptr;
		if (Object->TryGetArrayField(FieldName, ArrayPtr))
		{
			return ArrayPtr;
		}
		return nullptr;
	}

	EDesign2UMGPrototypeSerializedValueDomain GetSerializedValueDomain(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return EDesign2UMGPrototypeSerializedValueDomain::Unknown;
		}

		const FString ResolvedType = GetJsonString(Object, TEXT("resolvedType"));
		if (!ResolvedType.IsEmpty())
		{
			return ParseSerializedValueDomain(ResolvedType);
		}

		return ParseSerializedValueDomain(GetJsonString(Object, TEXT("type")));
	}

	bool TryConvertJsonValueToRuntimeValue(
		const TSharedPtr<FJsonValue>& JsonValue,
		const TMap<FString, FString>& VariableValues,
		const TMap<FString, FString>& VariableModes,
		FDesign2UMGPrototypeRuntimeValue& OutValue,
		TSet<FString>& AliasGuard);

	bool TryConvertJsonObjectToRuntimeValue(
		const TSharedPtr<FJsonObject>& Object,
		const TMap<FString, FString>& VariableValues,
		const TMap<FString, FString>& VariableModes,
		FDesign2UMGPrototypeRuntimeValue& OutValue,
		TSet<FString>& AliasGuard)
	{
		if (!Object.IsValid())
		{
			return false;
		}

		switch (GetSerializedValueDomain(Object))
		{
		case EDesign2UMGPrototypeSerializedValueDomain::Boolean:
		{
			bool bValue = false;
			if (Object->TryGetBoolField(TEXT("value"), bValue))
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
				OutValue.BoolValue = bValue;
				return true;
			}
			break;
		}
		case EDesign2UMGPrototypeSerializedValueDomain::Number:
		{
			double NumberValue = 0.0;
			if (Object->TryGetNumberField(TEXT("value"), NumberValue))
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Number;
				OutValue.NumberValue = NumberValue;
				return true;
			}
			break;
		}
		case EDesign2UMGPrototypeSerializedValueDomain::String:
		{
			FString StringValue;
			if (Object->TryGetStringField(TEXT("value"), StringValue))
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::String;
				OutValue.StringValue = StringValue;
				return true;
			}
			break;
		}
		case EDesign2UMGPrototypeSerializedValueDomain::Color:
		{
			const TSharedPtr<FJsonObject> ColorObject = GetJsonObject(Object, TEXT("value"));
			if (ColorObject.IsValid())
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Color;
				double ComponentValue = 0.0;
				if (ColorObject->TryGetNumberField(TEXT("r"), ComponentValue))
				{
					OutValue.ColorValue.R = static_cast<float>(ComponentValue);
				}
				if (ColorObject->TryGetNumberField(TEXT("g"), ComponentValue))
				{
					OutValue.ColorValue.G = static_cast<float>(ComponentValue);
				}
				if (ColorObject->TryGetNumberField(TEXT("b"), ComponentValue))
				{
					OutValue.ColorValue.B = static_cast<float>(ComponentValue);
				}
				OutValue.ColorValue.A = ColorObject->TryGetNumberField(TEXT("a"), ComponentValue)
					? static_cast<float>(ComponentValue)
					: 1.0f;
				return true;
			}
			break;
		}
		case EDesign2UMGPrototypeSerializedValueDomain::VariableAlias:
		{
			FString AliasId = GetJsonString(Object, TEXT("id"));
			if (AliasId.IsEmpty())
			{
				if (const TSharedPtr<FJsonObject> AliasObject = GetJsonObject(Object, TEXT("value")))
				{
					AliasId = GetJsonString(AliasObject, TEXT("id"));
				}
			}

			if (!AliasId.IsEmpty() && !AliasGuard.Contains(AliasId))
			{
				AliasGuard.Add(AliasId);
				const FString* StoredValueJson = VariableValues.Find(AliasId);
				if (StoredValueJson)
				{
					TSharedPtr<FJsonValue> AliasValue;
					if (TryParseJsonValue(*StoredValueJson, AliasValue)
						&& TryConvertJsonValueToRuntimeValue(AliasValue, VariableValues, VariableModes, OutValue, AliasGuard))
					{
						AliasGuard.Remove(AliasId);
						return true;
					}
				}
				AliasGuard.Remove(AliasId);
			}
			break;
		}
		case EDesign2UMGPrototypeSerializedValueDomain::Expression:
		{
			const TSharedPtr<FJsonObject> ExpressionObject = GetJsonObject(Object, TEXT("value"));
			const TSharedPtr<FJsonObject> EffectiveExpressionObject =
				ExpressionObject.IsValid() ? ExpressionObject : Object;
			const EDesign2UMGPrototypeExpressionFunction ExpressionFunction =
				ParseExpressionFunctionToken(GetJsonString(EffectiveExpressionObject, TEXT("expressionFunction")));
			const TArray<TSharedPtr<FJsonValue>>* Arguments =
				GetJsonArray(EffectiveExpressionObject, TEXT("expressionArguments"));

			TArray<FDesign2UMGPrototypeRuntimeValue> ResolvedArgs;
			if (Arguments)
			{
				for (const TSharedPtr<FJsonValue>& ArgumentValue : *Arguments)
				{
					FDesign2UMGPrototypeRuntimeValue ResolvedArg;
					if (!TryConvertJsonValueToRuntimeValue(ArgumentValue, VariableValues, VariableModes, ResolvedArg, AliasGuard))
					{
						return false;
					}
					ResolvedArgs.Add(MoveTemp(ResolvedArg));
				}
			}

			auto ToBool = [](const FDesign2UMGPrototypeRuntimeValue& Value) -> bool
			{
				switch (Value.Kind)
				{
				case EDesign2UMGPrototypeRuntimeValueKind::Boolean: return Value.BoolValue;
				case EDesign2UMGPrototypeRuntimeValueKind::Number:  return !FMath::IsNearlyZero(Value.NumberValue);
				case EDesign2UMGPrototypeRuntimeValueKind::String:  return !Value.StringValue.IsEmpty();
				case EDesign2UMGPrototypeRuntimeValueKind::Color:   return Value.ColorValue.A > KINDA_SMALL_NUMBER;
				default: return false;
				}
			};

			auto EqualsValue = [](const FDesign2UMGPrototypeRuntimeValue& A, const FDesign2UMGPrototypeRuntimeValue& B) -> bool
			{
				if (A.Kind != B.Kind)
				{
					if (A.Kind == EDesign2UMGPrototypeRuntimeValueKind::Number && B.Kind == EDesign2UMGPrototypeRuntimeValueKind::Boolean)
					{
						return FMath::IsNearlyEqual(A.NumberValue, B.BoolValue ? 1.0 : 0.0);
					}
					if (A.Kind == EDesign2UMGPrototypeRuntimeValueKind::Boolean && B.Kind == EDesign2UMGPrototypeRuntimeValueKind::Number)
					{
						return FMath::IsNearlyEqual(A.BoolValue ? 1.0 : 0.0, B.NumberValue);
					}
					return false;
				}
				switch (A.Kind)
				{
				case EDesign2UMGPrototypeRuntimeValueKind::Boolean: return A.BoolValue == B.BoolValue;
				case EDesign2UMGPrototypeRuntimeValueKind::Number:  return FMath::IsNearlyEqual(A.NumberValue, B.NumberValue);
				case EDesign2UMGPrototypeRuntimeValueKind::String:  return A.StringValue == B.StringValue;
				case EDesign2UMGPrototypeRuntimeValueKind::Color:   return A.ColorValue.Equals(B.ColorValue);
				default: return false;
				}
			};

			if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::And)
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
				OutValue.BoolValue = true;
				for (const FDesign2UMGPrototypeRuntimeValue& Value : ResolvedArgs)
				{
					if (!ToBool(Value)) { OutValue.BoolValue = false; break; }
				}
				return true;
			}
			if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::Or)
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
				OutValue.BoolValue = false;
				for (const FDesign2UMGPrototypeRuntimeValue& Value : ResolvedArgs)
				{
					if (ToBool(Value)) { OutValue.BoolValue = true; break; }
				}
				return true;
			}
			if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::Not && ResolvedArgs.Num() >= 1)
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
				OutValue.BoolValue = !ToBool(ResolvedArgs[0]);
				return true;
			}
			if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::Equals && ResolvedArgs.Num() >= 2)
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
				OutValue.BoolValue = EqualsValue(ResolvedArgs[0], ResolvedArgs[1]);
				return true;
			}
			if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::NotEquals && ResolvedArgs.Num() >= 2)
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
				OutValue.BoolValue = !EqualsValue(ResolvedArgs[0], ResolvedArgs[1]);
				return true;
			}
			if ((ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::GreaterThan
				|| ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::GreaterThanOrEqual
				|| ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::LessThan
				|| ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::LessThanOrEqual)
				&& ResolvedArgs.Num() >= 2
				&& ResolvedArgs[0].Kind == EDesign2UMGPrototypeRuntimeValueKind::Number
				&& ResolvedArgs[1].Kind == EDesign2UMGPrototypeRuntimeValueKind::Number)
			{
				OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
				if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::GreaterThan)
				{
					OutValue.BoolValue = ResolvedArgs[0].NumberValue > ResolvedArgs[1].NumberValue;
				}
				else if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::GreaterThanOrEqual)
				{
					OutValue.BoolValue = ResolvedArgs[0].NumberValue >= ResolvedArgs[1].NumberValue;
				}
				else if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::LessThan)
				{
					OutValue.BoolValue = ResolvedArgs[0].NumberValue < ResolvedArgs[1].NumberValue;
				}
				else
				{
					OutValue.BoolValue = ResolvedArgs[0].NumberValue <= ResolvedArgs[1].NumberValue;
				}
				return true;
			}
			if (ExpressionFunction == EDesign2UMGPrototypeExpressionFunction::VarModeLookup && ResolvedArgs.Num() >= 2)
			{
				const FString CollectionId =
					ResolvedArgs[0].Kind == EDesign2UMGPrototypeRuntimeValueKind::String
						? ResolvedArgs[0].StringValue : FString();
				const FString ExpectedModeId =
					ResolvedArgs[1].Kind == EDesign2UMGPrototypeRuntimeValueKind::String
						? ResolvedArgs[1].StringValue : FString();
				const FString* ActiveModeId = VariableModes.Find(CollectionId);
				if (ActiveModeId)
				{
					OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
					OutValue.BoolValue = *ActiveModeId == ExpectedModeId;
					return true;
				}
			}
			break;
		}
		default:
			break;
		}

		return false;
	}

	bool TryConvertJsonValueToRuntimeValue(
		const TSharedPtr<FJsonValue>& JsonValue,
		const TMap<FString, FString>& VariableValues,
		const TMap<FString, FString>& VariableModes,
		FDesign2UMGPrototypeRuntimeValue& OutValue,
		TSet<FString>& AliasGuard)
	{
		if (!JsonValue.IsValid())
		{
			return false;
		}
		if (JsonValue->Type == EJson::Boolean)
		{
			OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Boolean;
			OutValue.BoolValue = JsonValue->AsBool();
			return true;
		}
		if (JsonValue->Type == EJson::Number)
		{
			OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::Number;
			OutValue.NumberValue = JsonValue->AsNumber();
			return true;
		}
		if (JsonValue->Type == EJson::String)
		{
			OutValue.Kind = EDesign2UMGPrototypeRuntimeValueKind::String;
			OutValue.StringValue = JsonValue->AsString();
			return true;
		}
		if (JsonValue->Type == EJson::Object)
		{
			return TryConvertJsonObjectToRuntimeValue(
				JsonValue->AsObject(), VariableValues, VariableModes, OutValue, AliasGuard);
		}
		return false;
	}
}

namespace Design2UMGPrototypeRuntimeValueResolver
{
	bool EvaluateConditionJson(
		const FString& ConditionJson,
		const TMap<FString, FString>& VariableValues,
		const TMap<FString, FString>& VariableModes)
	{
		if (ConditionJson.IsEmpty())
		{
			return true;
		}

		TSharedPtr<FJsonValue> ConditionValue;
		if (!TryParseJsonValue(ConditionJson, ConditionValue))
		{
			return false;
		}

		TSet<FString> AliasGuard;
		FDesign2UMGPrototypeRuntimeValue ResolvedValue;
		if (!TryConvertJsonValueToRuntimeValue(ConditionValue, VariableValues, VariableModes, ResolvedValue, AliasGuard))
		{
			return false;
		}

		switch (ResolvedValue.Kind)
		{
		case EDesign2UMGPrototypeRuntimeValueKind::Boolean: return ResolvedValue.BoolValue;
		case EDesign2UMGPrototypeRuntimeValueKind::Number:  return !FMath::IsNearlyZero(ResolvedValue.NumberValue);
		case EDesign2UMGPrototypeRuntimeValueKind::String:  return !ResolvedValue.StringValue.IsEmpty();
		case EDesign2UMGPrototypeRuntimeValueKind::Color:   return ResolvedValue.ColorValue.A > KINDA_SMALL_NUMBER;
		default: return false;
		}
	}
}
