// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

// Runtime variable/json value resolution owner for prototype navigation.
// Owns: typed value conversion, alias resolution, expression evaluation, condition evaluation.

namespace Design2UMGPrototypeRuntimeValueResolver
{
	bool EvaluateConditionJson(
		const FString& ConditionJson,
		const TMap<FString, FString>& VariableValues,
		const TMap<FString, FString>& VariableModes);
}
