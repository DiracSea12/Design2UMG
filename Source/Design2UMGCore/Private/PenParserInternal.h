// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "Misc/DefaultValueHelper.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// Shared helpers used by the PenParser*.cpp translation units.
// Not part of the public API; include only from PenParser*.cpp files.

namespace FlexibleFieldParsing
{
	inline bool TryParseNumericString(const FString& InValue, double& OutValue)
	{
		const FString Trimmed = InValue.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
			return false;
		double Parsed = 0.0;
		if (!FDefaultValueHelper::ParseDouble(Trimmed, Parsed))
			return false;
		OutValue = Parsed;
		return true;
	}

	inline bool TryParseBooleanString(const FString& InValue, bool& OutValue)
	{
		const FString Normalized = InValue.TrimStartAndEnd().ToLower();
		if (Normalized == TEXT("true") || Normalized == TEXT("1") || Normalized == TEXT("yes") || Normalized == TEXT("on"))
		{
			OutValue = true;
			return true;
		}
		if (Normalized == TEXT("false") || Normalized == TEXT("0") || Normalized == TEXT("no") || Normalized == TEXT("off"))
		{
			OutValue = false;
			return true;
		}
		return false;
	}

	inline bool TryGetNumberValueFlexible(const TSharedPtr<FJsonValue>& Value, double& OutValue)
	{
		if (!Value.IsValid())
			return false;
		if (Value->Type == EJson::Number)
		{
			OutValue = Value->AsNumber();
			return true;
		}
		if (Value->Type == EJson::String)
			return TryParseNumericString(Value->AsString(), OutValue);
		return false;
	}

	template <typename NumberType>
	bool TryGetNumberValueFlexible(const TSharedPtr<FJsonValue>& Value, NumberType& OutValue)
	{
		double Parsed = 0.0;
		if (!TryGetNumberValueFlexible(Value, Parsed))
			return false;
		OutValue = static_cast<NumberType>(Parsed);
		return true;
	}

	template <typename NumberType>
	bool TryGetNumberFieldFlexible(
		const TSharedPtr<FJsonObject>& Obj,
		const FString& FieldName,
		NumberType& OutValue)
	{
		if (!Obj.IsValid() || !Obj->HasField(FieldName))
			return false;
		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(FieldName);
		if (!FieldValue.IsValid())
			return false;
		double Parsed = 0.0;
		if (!TryGetNumberValueFlexible(FieldValue, Parsed))
			return false;
		OutValue = static_cast<NumberType>(Parsed);
		return true;
	}

	template <typename NumberType>
	bool TryGetResolvedNumberField(
		const TSharedPtr<FJsonObject>& Obj,
		const FString& FieldName,
		const FDesignVariableResolver& Resolver,
		NumberType& OutValue)
	{
		if (!Obj.IsValid() || !Obj->HasField(FieldName))
			return false;
		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(FieldName);
		if (!FieldValue.IsValid())
			return false;
		if (TryGetNumberValueFlexible(FieldValue, OutValue))
			return true;
		if (FieldValue->Type != EJson::String)
			return false;
		const FString RawValue = FieldValue->AsString();
		if (!FDesignVariableResolver::IsVariableRef(RawValue))
			return false;
		OutValue = static_cast<NumberType>(Resolver.ResolveNumber(RawValue));
		return true;
	}

	inline bool TryGetResolvedBoolField(
		const TSharedPtr<FJsonObject>& Obj,
		const FString& FieldName,
		const FDesignVariableResolver& Resolver,
		bool& OutValue)
	{
		if (!Obj.IsValid() || !Obj->HasField(FieldName))
			return false;
		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(FieldName);
		if (!FieldValue.IsValid())
			return false;
		if (FieldValue->Type == EJson::Boolean)
		{
			OutValue = FieldValue->AsBool();
			return true;
		}
		if (FieldValue->Type != EJson::String)
			return false;
		const FString RawValue = FieldValue->AsString();
		if (TryParseBooleanString(RawValue, OutValue))
			return true;
		if (!FDesignVariableResolver::IsVariableRef(RawValue))
			return false;
		return TryParseBooleanString(Resolver.ResolveString(RawValue), OutValue);
	}
}

using FlexibleFieldParsing::TryGetNumberFieldFlexible;
using FlexibleFieldParsing::TryGetNumberValueFlexible;
using FlexibleFieldParsing::TryGetResolvedBoolField;
using FlexibleFieldParsing::TryGetResolvedNumberField;

// Cross-translation-unit parse functions shared by the PenParser*.cpp files.
// Defined in the .cpp file named per group below.
namespace PenParser
{
	// PenParserEnums.cpp
	EDesignNodeType ParseNodeType(const FString& Token);
	EDesignLayout ParseLayout(const FString& Token);
	EDesignTextAlignVertical ParseTextAlignVertical(const FString& Token);
	EDesignTextGrowth ParseTextGrowth(const FString& Token);

	// PenParserStyling.cpp
	FDesignFill ParseFill(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);
	FDesignPadding ParsePadding(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);

	// PenParserNodeProperties.cpp
	void ApplyNodeBoxProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, EDesignNodeType SizeNodeType, const FDesignVariableResolver& Resolver);
	void ApplyNodeLayoutProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver, bool bIncludeTextProperties);
	void ApplyNodeTextStyleProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver);

	// PenParser.cpp
	FDesignSize ParseSize(const TSharedPtr<FJsonValue>& Value, EDesignNodeType NodeType, const FDesignVariableResolver& Resolver);
	TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver);
	void AppendParsedChildren(const TSharedPtr<FJsonValue>& ChildrenField, const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& OutChildren);
	void ParseNodeFillProperties(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver);
	void ParseNodeStrokeProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver);
}
