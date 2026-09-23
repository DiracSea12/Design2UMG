// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorCommandOwner.h"

#include "Automation/Design2UMGAutomationRender.h"
#include "Design2UMGEditorImportOrchestrator.h"
#include "Design2UMGLog.h"
#include "Design2UMGWidgetBindingTools.h"

#include "HAL/IConsoleManager.h"

namespace
{
	using namespace Design2UMGEditorImportOrchestrator;

	// Built-in design sources accepted by Design2UMG.ImportSource, in the order
	// they used to be registered. Mirrors the direct parser dispatch in
	// RunRegisteredSourceImport ("Pen" -> FPenParser, "Figma" -> FFigmaParser).
	const TCHAR* const GImportSourceIds[] = { TEXT("Pen"), TEXT("Figma") };

	void ApplyOutputPreviewArg(const FString& Arg, FString& OutputPath, bool& bUsePreview)
	{
		if (Arg.StartsWith(TEXT("--out=")))
		{
			OutputPath = Arg.RightChop(6);
		}
		else if (Arg == TEXT("--preview"))
		{
			bUsePreview = true;
		}
		else if (Arg == TEXT("--no-preview"))
		{
			bUsePreview = false;
		}
	}

	bool ParsePenConvertCommandArgs(const TArray<FString>& Args, FPenConvertRequest& OutRequest)
	{
		if (Args.Num() < 2)
		{
			return false;
		}

		OutRequest.NodesPath = Args[0];
		OutRequest.VarsPath = Args[1];

		for (int32 Index = 2; Index < Args.Num(); ++Index)
		{
			const FString& Arg = Args[Index];
			if (Arg.StartsWith(TEXT("--")))
			{
				ApplyOutputPreviewArg(Arg, OutRequest.OutputPath, OutRequest.bUsePreview);
			}
			else if (OutRequest.TargetNodeId.IsEmpty())
			{
				OutRequest.TargetNodeId = Arg;
			}
		}

		return true;
	}

	void ParsePenImportCommandArgs(const TArray<FString>& Args, const int32 StartIndex, FPenImportRequest& OutRequest)
	{
		for (int32 Index = StartIndex; Index < Args.Num(); ++Index)
		{
			const FString& Arg = Args[Index];
			if (Arg.StartsWith(TEXT("--node=")))
			{
				OutRequest.TargetNodeId = Arg.RightChop(7);
			}
			else if (Arg == TEXT("--no-png"))
			{
				OutRequest.bRenderPng = false;
			}
			else if (Arg == TEXT("--png"))
			{
				OutRequest.bRenderPng = true;
			}
			else
			{
				ApplyOutputPreviewArg(Arg, OutRequest.OutputPath, OutRequest.bUsePreview);
			}
		}
	}
}

namespace Design2UMGEditorCommandOwner
{
	void RegisterConsoleCommands()
	{
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.PenConvert"),
			TEXT("Convert from MCP JSON: Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				FPenConvertRequest Request;
				if (!ParsePenConvertCommandArgs(Args, Request))
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.PenConvert <nodes.json> <vars.json> [TargetNodeId] [--preview|--no-preview] [--out=/Game/Path]"));
					return;
				}

				RunPenConvert(Request);
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.Import"),
			TEXT("Import .pen file: Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview] [--no-png]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.Import <file.pen> [--node=<id>] [--out=/Game/Path] [--no-preview] [--no-png]"));
					return;
				}

				FPenImportRequest Request;
				Request.FilePath = Args[0];
				ParsePenImportCommandArgs(Args, 1, Request);
				RunPenImport(Request);
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.ImportSource"),
			TEXT("Import from registered source: Design2UMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogDesign2UMG, Log, TEXT("Usage: Design2UMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"));
					UE_LOG(LogDesign2UMG, Log, TEXT("Registered sources (%d):"), (int32)UE_ARRAY_COUNT(GImportSourceIds));
					for (const TCHAR* const SourceId : GImportSourceIds)
					{
						UE_LOG(LogDesign2UMG, Log, TEXT("  - %s"), SourceId);
					}
					return;
				}

				if (Args.Num() < 2)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.ImportSource <SourceId> <json_file> [--out=/Game/Path] [--no-preview]"));
					return;
				}

				FRegisteredSourceImportRequest Request;
				Request.SourceId = FName(*Args[0]);
				Request.JsonPath = Args[1];
				for (int32 Index = 2; Index < Args.Num(); ++Index)
				{
					ApplyOutputPreviewArg(Args[Index], Request.OutputPath, Request.bUsePreview);
				}

				RunRegisteredSourceImport(Request);
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.FigmaImport"),
			TEXT("Import from Figma: Design2UMG.FigmaImport <file_url_or_key> <token_from_secure_source>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.FigmaImport <file_url_or_key> <token_from_secure_source>"));
					return;
				}

				RunFigmaImport(Args[0], Args[1]);
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.LocalFigmaImport"),
			TEXT("Import local Figma export: Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 1)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.LocalFigmaImport <design.json> [--out=/Game/Path] [--no-preview]"));
					return;
				}

				FLocalFigmaImportRequest Request;
				Request.JsonFilePath = Args[0];
				for (int32 Index = 1; Index < Args.Num(); ++Index)
				{
					ApplyOutputPreviewArg(Args[Index], Request.OutputPath, Request.bUsePreview);
				}

				RunLocalFigmaImport(Request);
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.Reimport"),
			TEXT("Reimport design.json into an existing generated WBP: Design2UMG.Reimport <TargetWBPAssetPath> <design.json> [--resolution-copy]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				if (Args.Num() < 2)
				{
					UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.Reimport <TargetWBPAssetPath> <design.json> [--resolution-copy]"));
					return;
				}

				bool bGenerateResolutionCopy = false;
				for (int32 Index = 2; Index < Args.Num(); ++Index)
				{
					if (Args[Index] == TEXT("--resolution-copy"))
					{
						bGenerateResolutionCopy = true;
					}
				}

				RunDesignReimport(Args[0], Args[1], bGenerateResolutionCopy);
			}),
			ECVF_Default);

		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.AutomationRender"),
			TEXT("Automation render from manifest JSON: Design2UMG.AutomationRender <spec.json>"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				Design2UMGAutomationRender::HandleConsoleCommand(Args);
			}),
			ECVF_Default);

		// Automated construction channel for WBP event bindings (FDelegateEditorBinding): the only scripted entry
		// outside the editor UI (black-box binding scenarios / command-line validation), with no special-casing logic.
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("Design2UMG.WidgetBindings"),
			TEXT("Inspect/add WBP widget event bindings: Design2UMG.WidgetBindings <WBPAssetPath> [--add <WidgetName> <PropertyName> <FunctionName>]"),
			FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
			{
				Design2UMGWidgetBindingTools::HandleConsoleCommand(Args);
			}),
			ECVF_Default);
	}

	void UnregisterConsoleCommands()
	{
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.Import"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.PenConvert"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.ImportSource"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.FigmaImport"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.LocalFigmaImport"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.Reimport"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.AutomationRender"), false);
		IConsoleManager::Get().UnregisterConsoleObject(TEXT("Design2UMG.WidgetBindings"), false);
	}
}
