// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Containers/UnrealString.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

namespace Design2UMGWidgetMetadata
{
inline decltype(auto) PackageMetadata(UObject* Object)
{
#if UE_VERSION_OLDER_THAN(5, 6, 0)
    return *Object->GetOutermost()->GetMetaData();
#else
    return Object->GetOutermost()->GetMetaData();
#endif
}

inline constexpr const TCHAR* BackgroundCaptureInsetsKey = TEXT("D2UBackgroundCaptureInsets");

// Consumed during generation; generated widgets do not use package metadata at runtime.
inline void SetBackgroundCaptureInsets(UObject* Widget, const FMargin& Insets)
{
    const FString Value = FString::Printf(TEXT("%.9g,%.9g,%.9g,%.9g"), Insets.Left, Insets.Top, Insets.Right, Insets.Bottom);
    PackageMetadata(Widget).SetValue(Widget, BackgroundCaptureInsetsKey, *Value);
}

inline bool ConsumeBackgroundCaptureInsets(UObject* Widget, TOptional<FMargin>& Out, FString& Error)
{
    Out.Reset();
    auto& Metadata = PackageMetadata(Widget);
    const FString Value = Metadata.GetValue(Widget, BackgroundCaptureInsetsKey);
    Metadata.RemoveValue(Widget, BackgroundCaptureInsetsKey);
    if (Value.IsEmpty()) return true;
    TArray<FString> Parts;
    Value.ParseIntoArray(Parts, TEXT(","), false);
    float Values[4];
    bool Valid = Parts.Num() == 4;
    for (int32 Index = 0; Valid && Index < 4; ++Index)
        Valid = LexTryParseString(Values[Index], *Parts[Index]) && FMath::IsFinite(Values[Index]) && Values[Index] >= 0;
    if (!Valid)
    {
        Error = TEXT("Invalid background capture insets: ") + Widget->GetPathName();
        return false;
    }
    Out = FMargin(Values[0], Values[1], Values[2], Values[3]);
    return true;
}
}
