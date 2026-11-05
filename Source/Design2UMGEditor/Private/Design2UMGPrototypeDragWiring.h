// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "Design2UMGPrototypeGraphWiringInternal.h"

struct FUMGPrototypeNavigationBinding;

namespace Design2UMGPrototypeGraphWiring
{
struct FDragCandidate
{
    const FUMGPrototypeNavigationBinding* Binding = nullptr;
    const FResolvedBinding* Resolved = nullptr;
    FName CommitEventName;
    FName CancelEventName;
};

// The accepted flag remains set through OnClicked and is reset on the next press.
FName GetDragAcceptedName(int32 LeaderBindingIndex);
FName GetDragSelectedName(int32 LeaderBindingIndex);
FString GetDragStableIdentity(const FUMGPrototypeNavigationBinding& Binding);
FName GetDragSpanName(const FString& StableIdentity);
FName GetDragCommitThresholdName(const FString& StableIdentity);
FName GetDragSlateThresholdName(const FString& StableIdentity);

// Call once per same-hotspot group, before the final skeleton refresh.
bool PrepareDragVariables(UWidgetBlueprint* WBP, int32 LeaderBindingIndex,
    const FString& StableIdentity, TArray<FString>& OutWarnings);

// Callbacks are supplied by the navigation graph owner, one pair per target.
bool WireDragBinding(UEdGraph* Graph, UWidgetBlueprint* WBP, TConstArrayView<FDragCandidate> Candidates,
    const FBindingGraphTargets& Targets, const FEngineFunctions& Functions, TArray<FString>& OutWarnings);

// Stops a group's monitor and pending completions without resetting click suppression.
bool AppendDragCleanup(UEdGraph* Graph, UWidgetBlueprint* WBP, TConstArrayView<FDragCandidate> Candidates,
    const FEngineFunctions& Functions, FChainLayout& Layout);
}
