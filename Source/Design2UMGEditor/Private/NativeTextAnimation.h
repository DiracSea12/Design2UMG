// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once
#include "UMGPrototypeAnimationBuilder.h"
class UTextBlock;
class URetainerBox;
class UFunction;

namespace NativeTextAnimation
{
/**
 * Native font and paint values are blueprint members evaluated by MovieScene; one refresh function per clip pushes
 * them to Slate at the end of each evaluation. Members and tracks are added immediately, while the refresh functions
 * are built together, so the skeleton regenerates twice rather than once per member and function.
 */
class FRefreshBuilder
{
public:
    explicit FRefreshBuilder(UWidgetBlueprint* InBlueprint) : Blueprint(InBlueprint) {}

    bool AddFontTracks(UWidgetAnimation* Animation, UTextBlock* Source, UTextBlock* Destination, bool bSize, bool bTracking,
        UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error);
    bool AddPaintTrack(UWidgetAnimation* Animation, URetainerBox* Retainer, int32 TargetIndex,
        UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error);
    // Native Image/Border brush paint or Retainer effect, with the logical owner size evaluated on the same curve.
    bool AddSurfacePaintTrack(UWidgetAnimation* Animation, UWidget* Surface, int32 TargetIndex,
        const FVector2D& SourceSize, const FVector2D& DestinationSize,
        UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error);
    /** Builds the refresh function of every clip the blueprint still owns, reporting clips whose graph cannot link. */
    void Build(TFunctionRef<void(UWidgetAnimation*, const FString&)> OnFailed);

private:
    struct FChannel { FName Member; FName Parameter; float Start; float End; };
    struct FFontRefresh { UTextBlock* Text; FName Tracking; };
    struct FPaintRefresh { UWidget* Owner; UFunction* Material; int32 TargetIndex; TArray<FChannel> Channels; };
    struct FClipRefresh { TArray<FFontRefresh> Fonts; TArray<FPaintRefresh> Paints; };

    bool AddMember(UWidgetAnimation* Animation, const FChannel& Channel,
        UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error);
    bool AddPaint(UWidgetAnimation* Animation, UWidget* Owner, UFunction* Material, int32 TargetIndex,
        TArray<FChannel> Channels, UMGPrototypeAnimationBuilder::FChannelWriter WriteChannel, FString& Error);
    bool BuildClip(UWidgetAnimation* Animation, const FClipRefresh& Clip) const;

    UWidgetBlueprint* Blueprint;
    TMap<TWeakObjectPtr<UWidgetAnimation>, FClipRefresh> Clips;
};

// Only native refresh functions referenced by this clip's owned repeater endpoints.
TArray<FName> RefreshFunctions(UWidgetBlueprint* Blueprint, UWidgetAnimation* Animation);
void RemoveGeneratedEndpoints(UWidgetBlueprint* Blueprint, UWidgetAnimation* Animation);
}
