// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "Editor/TransBuffer.h"
#include "Design2UMGTestTransBuffer.generated.h"

// Test double at Unreal's transaction boundary; production code uses UTransactor unchanged.
UCLASS()
class UDesign2UMGTestTransBuffer : public UTransBuffer
{
	GENERATED_BODY()
public:
	bool bRejectUndo = false;
	virtual bool Undo(bool bCanRedo = true) override
	{
		return !bRejectUndo && Super::Undo(bCanRedo);
	}
};
