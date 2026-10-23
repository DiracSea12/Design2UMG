// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "PencilFontProcess.h"
#include "DesignLocalFontImport.h"
#include "DesignFontImportPolicy.h"
#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
    bool RunPencilFontWorker(const TSharedRef<FJsonObject>& Request, const FString& Directory,
                            TSharedPtr<FJsonObject>& Response, FString& OutError)
    {
        const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
        if (!Plugin)
        {
            OutError = TEXT("Design2UMG plugin directory was not found for font conversion.");
            return false;
        }
        const FString Worker = FPaths::ConvertRelativePathToFull(
            Plugin->GetBaseDir() / TEXT("Resources/PencilFontWorker/Win64/PencilFontWorker.exe"));
        const FString RequestPath = Directory / TEXT("request.json");
        const FString ResponsePath = Directory / TEXT("response.json");
        FString RequestText;
        if (!FJsonSerializer::Serialize(Request, TJsonWriterFactory<>::Create(&RequestText)) ||
            !FFileHelper::SaveStringToFile(RequestText, *RequestPath,
                                         FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            OutError = TEXT("Could not write font conversion request: ") + RequestPath;
            return false;
        }
        const FString Arguments = FString::Printf(TEXT("--request \"%s\" --response \"%s\""),
                                                  *RequestPath, *ResponsePath);
        FProcHandle Process = FPlatformProcess::CreateProc(*Worker, *Arguments, false, true, true, nullptr, 0,
                                                          *FPaths::GetPath(Worker), nullptr);
        if (!Process.IsValid())
        {
            OutError = TEXT("Could not start the bundled font converter: ") + Worker;
            return false;
        }
        ON_SCOPE_EXIT { FPlatformProcess::CloseProc(Process); };
            const double Deadline = FPlatformTime::Seconds() + DesignFontImportPolicy::WorkerTimeoutSeconds;
        FScopedSlowTask Progress(1, NSLOCTEXT("Design2UMG", "FontConversion", "Preparing font..."));
        while (FPlatformProcess::IsProcRunning(Process))
        {
            // Update only the enclosing import progress; no converter window or dialog is created.
            Progress.EnterProgressFrame(0);
            const bool Cancelled = Progress.ShouldCancel();
            if (Cancelled || FPlatformTime::Seconds() >= Deadline)
            {
                FPlatformProcess::TerminateProc(Process, true);
                FPlatformProcess::WaitForProc(Process);
                OutError = Cancelled ? TEXT("Font conversion cancelled.") : TEXT("Font conversion timed out.");
                return false;
            }
            FPlatformProcess::Sleep(0.01f);
        }
        int32 ExitCode = -1;
        FPlatformProcess::GetProcReturnCode(Process, &ExitCode);
        FString ResponseText;
        if (!FFileHelper::LoadFileToString(ResponseText, *ResponsePath) ||
            !DesignJsonUtils::TryParseObject(ResponseText, Response))
        {
            OutError = FString::Printf(TEXT("Font converter returned no valid response (exit %d)."), ExitCode);
            return false;
        }
        bool Success = false;
        if (!Response->TryGetBoolField(TEXT("success"), Success) || !Success)
        {
            Response->TryGetStringField(TEXT("error"), OutError);
            if (OutError.IsEmpty()) OutError = TEXT("Font conversion failed without an error description.");
            return false;
        }
        if (ExitCode != 0)
        {
            OutError = FString::Printf(TEXT("Font worker returned exit %d."), ExitCode);
            return false;
        }
        return true;
    }
}

bool PencilFontProcess::ConvertToStaticFont(const FString& InputPath, const FString& OutputPath,
                                          int32 SourceWeight, int32 ExpectedWeight, FString& OutError)
{
    const auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("input_path"), InputPath);
    Request->SetStringField(TEXT("output_path"), OutputPath);
    Request->SetNumberField(TEXT("weight"), SourceWeight);
    TSharedPtr<FJsonObject> Response;
    if (!RunPencilFontWorker(Request, FPaths::GetPath(OutputPath), Response, OutError)) return false;
    double ActualWeight = 0;
    FString SourceHash, OutputHash;
    if (!Response->TryGetNumberField(TEXT("weight"), ActualWeight) || ActualWeight != ExpectedWeight ||
        !Response->TryGetStringField(TEXT("source_sha256"), SourceHash) ||
        !Response->TryGetStringField(TEXT("output_sha256"), OutputHash))
    {
        OutError = TEXT("Font converter output does not match the requested static style.");
        return false;
    }
    const int64 Size = IFileManager::Get().FileSize(*OutputPath);
    if (Size <= 0 || !DesignFontImportPolicy::FitsByteLimit(Size, 0))
    {
        OutError = FString::Printf(TEXT("Converted font is missing, empty or exceeds the %d MiB font limit."),
                                   DesignFontImportPolicy::MaxFontBytes / (1024 * 1024));
        return false;
    }
    UE_LOG(LogDesign2UMG, Verbose, TEXT("Pencil font conversion: source=%s output=%s weight=%d"),
           *SourceHash, *OutputHash, ExpectedWeight);
    return true;
}

bool PencilFontProcess::InspectFont(const FString& InputPath, DesignLocalFontImport::FFace& OutFace, FString& OutError)
{
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()
        / TEXT("Design2UMG/FontImport") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    if (!IFileManager::Get().MakeDirectory(*Directory, true))
    {
        OutError = TEXT("Could not create font metadata request directory: ") + Directory;
        return false;
    }
    ON_SCOPE_EXIT
    {
        if (!IFileManager::Get().DeleteDirectory(*Directory, false, true))
            UE_LOG(LogDesign2UMG, Warning, TEXT("Could not remove font metadata request files: %s"), *Directory);
    };
    const auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("action"), TEXT("inspect"));
    Request->SetStringField(TEXT("input_path"), FPaths::ConvertRelativePathToFull(InputPath));
    TSharedPtr<FJsonObject> Response;
    if (!RunPencilFontWorker(Request, Directory, Response, OutError)) return false;
    OutFace = DesignLocalFontImport::FFace();
    OutFace.Path = InputPath;
    OutFace.Family = Response->GetStringField(TEXT("family"));
    OutFace.PostScriptName = Response->GetStringField(TEXT("postscript"));
    OutFace.Weight = Response->GetIntegerField(TEXT("weight"));
    OutFace.bItalic = Response->GetBoolField(TEXT("italic"));
    OutFace.bVariable = Response->GetBoolField(TEXT("variable"));
    double Minimum = 0, Maximum = 0;
    if (Response->TryGetNumberField(TEXT("weight_min"), Minimum) &&
        Response->TryGetNumberField(TEXT("weight_max"), Maximum))
        OutFace.WeightRange = FVector2D(Minimum, Maximum);
    if (OutFace.Family.IsEmpty())
    {
        OutError = TEXT("Font metadata has no family: ") + InputPath;
        return false;
    }
    return true;
}
