// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#include "PencilFontToolText.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FPencilFontToolText::FPencilFontToolText()
{
    const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
    ResourcePath = Plugin ? Plugin->GetBaseDir() / TEXT("Resources/FontToolText.json") : TEXT("Design2UMG/Resources/FontToolText.json");
    FString Json;
    TSharedPtr<FJsonObject> Object;
    if (!FFileHelper::LoadFileToString(Json, *ResourcePath) || !DesignJsonUtils::TryParseObject(Json, Object))
    { Error = ResourcePath + TEXT(" [JSON/read]"); return; }
    for (const auto& Pair : Object->Values)
    {
        FString Value;
        if (!Pair.Value->TryGetString(Value) || Value.IsEmpty())
        { Error = ResourcePath + TEXT(" [") + Pair.Key + TEXT(": string]"); return; }
        Values.Add(FString(*Pair.Key), MoveTemp(Value));
    }
}

FString FPencilFontToolText::Get(const TCHAR* Key, const TMap<FString, FString>& Arguments) const
{
    if (!Error.IsEmpty()) return Error;
    const FString* Found = Values.Find(Key);
    if (!Found) return ResourcePath + TEXT(" [") + Key + TEXT(": missing]");
    FString Result;
    for (int32 Cursor = 0; Cursor < Found->Len();)
    {
        const TCHAR C = (*Found)[Cursor];
        if (C == TEXT('}')) return ResourcePath + TEXT(" [") + Key + TEXT(": format]");
        if (C != TEXT('{')) { Result.AppendChar(C); ++Cursor; continue; }
        const int32 End = Found->Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor + 1);
        if (End == INDEX_NONE) return ResourcePath + TEXT(" [") + Key + TEXT(": format]");
        const FString* Value = Arguments.Find(Found->Mid(Cursor + 1, End - Cursor - 1));
        if (!Value) return ResourcePath + TEXT(" [") + Key + TEXT(": format]");
        Result += *Value;
        Cursor = End + 1;
    }
    return Result;
}
