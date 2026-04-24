#include "Automation/Design2UMGAutomationRender.h"

#include "Automation/Design2UMGAutomationStructureSnapshot.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignFontImportService.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "DesignPathRasterizer.h"
#include "DesignSchema.h"
#include "FigmaParser.h"
#include "FontResolver.h"
#include "PenMCPBridge.h"
#include "ProceduralShapeGenerator.h"
#include "UMGSchema.h"
#include "UMGWidgetUtils.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "WidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	bool GDesign2UMGAutomationRenderActive = false;

	struct FAutomationRenderSpec
	{
		FString Mode;
		FString SourcePath;
		FString NodeId;
		FString OutputPath;
		FString RenderPngPath;
		FString ResultJsonPath;
		FIntPoint RenderSize = FIntPoint::ZeroValue;
		bool bForceReloadPackage = false;
	};

	struct FAutomationRenderResult
	{
		bool bSuccess = false;
		FString Mode;
		FString SourcePath;
		FString GeneratedAssetPath;
		FString RenderPngPath;
		FString ErrorMessage;
		FIntPoint RenderSize = FIntPoint::ZeroValue;
		TSharedPtr<FJsonObject> SourceStructure;
		TSharedPtr<FJsonObject> AssetStructure;
	};

	const TCHAR* GeneratedAssetFromResultPrefix = TEXT("@generated_asset_from:");

	bool ShouldSkipDesignerOnlyNodeForAutomationSnapshot(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		switch (Node->Type)
		{
		case EDesignNodeType::Connection:
		case EDesignNodeType::Note:
		case EDesignNodeType::Prompt:
		case EDesignNodeType::Context:
			return true;
		default:
			return false;
		}
	}

	bool LoadAutomationRenderSpec(const FString& SpecPath, FAutomationRenderSpec& OutSpec, FString& OutError)
	{
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *SpecPath))
		{
			OutError = FString::Printf(TEXT("Failed to read automation spec: %s"), *SpecPath);
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse automation spec JSON: %s"), *SpecPath);
			return false;
		}

		Root->TryGetStringField(TEXT("mode"), OutSpec.Mode);
		Root->TryGetStringField(TEXT("source_path"), OutSpec.SourcePath);
		Root->TryGetStringField(TEXT("node_id"), OutSpec.NodeId);
		Root->TryGetStringField(TEXT("output_path"), OutSpec.OutputPath);
		Root->TryGetStringField(TEXT("render_png_path"), OutSpec.RenderPngPath);
		Root->TryGetStringField(TEXT("result_json_path"), OutSpec.ResultJsonPath);
		Root->TryGetBoolField(TEXT("force_reload_package"), OutSpec.bForceReloadPackage);

		double RenderWidth = 0.0;
		double RenderHeight = 0.0;
		Root->TryGetNumberField(TEXT("render_width"), RenderWidth);
		Root->TryGetNumberField(TEXT("render_height"), RenderHeight);
		if (RenderWidth > 1 && RenderHeight > 1)
		{
			OutSpec.RenderSize = FIntPoint(
				FMath::RoundToInt(RenderWidth),
				FMath::RoundToInt(RenderHeight));
		}

		if (OutSpec.Mode.IsEmpty())
		{
			OutError = TEXT("Automation spec missing 'mode'");
			return false;
		}
		if (OutSpec.SourcePath.IsEmpty())
		{
			OutError = TEXT("Automation spec missing 'source_path'");
			return false;
		}
		if (OutSpec.RenderPngPath.IsEmpty())
		{
			OutError = TEXT("Automation spec missing 'render_png_path'");
			return false;
		}

		return true;
	}

	bool WriteAutomationRenderResult(const FAutomationRenderSpec& Spec, const FAutomationRenderResult& Result)
	{
		if (Spec.ResultJsonPath.IsEmpty())
		{
			return true;
		}

		const FString OutputDir = FPaths::GetPath(Spec.ResultJsonPath);
		if (!OutputDir.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*OutputDir, true);
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("success"), Result.bSuccess);
		Root->SetStringField(TEXT("mode"), Result.Mode);
		Root->SetStringField(TEXT("source_path"), Result.SourcePath);
		Root->SetStringField(TEXT("generated_asset_path"), Result.GeneratedAssetPath);
		Root->SetStringField(TEXT("render_png_path"), Result.RenderPngPath);
		Root->SetStringField(TEXT("error_message"), Result.ErrorMessage);
		Root->SetNumberField(TEXT("render_width"), Result.RenderSize.X);
		Root->SetNumberField(TEXT("render_height"), Result.RenderSize.Y);
		if (Result.SourceStructure.IsValid())
		{
			Root->SetObjectField(TEXT("source_structure"), Result.SourceStructure.ToSharedRef());
		}
		if (Result.AssetStructure.IsValid())
		{
			Root->SetObjectField(TEXT("asset_structure"), Result.AssetStructure.ToSharedRef());
		}

		FString JsonStr;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
		if (!FJsonSerializer::Serialize(Root, Writer))
		{
			return false;
		}

		return FFileHelper::SaveStringToFile(JsonStr, *Spec.ResultJsonPath, FFileHelper::EEncodingOptions::ForceUTF8);
	}

	bool ResolveGeneratedAssetSourcePath(const FString& InSourcePath, FString& OutResolvedSourcePath, FString& OutError)
	{
		OutResolvedSourcePath = InSourcePath;
		if (!InSourcePath.StartsWith(GeneratedAssetFromResultPrefix))
		{
			return true;
		}

		const FString ResultJsonPath = InSourcePath.RightChop(FCString::Strlen(GeneratedAssetFromResultPrefix)).TrimStartAndEnd();
		if (ResultJsonPath.IsEmpty())
		{
			OutError = TEXT("Missing result json path after @generated_asset_from:");
			return false;
		}

		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *ResultJsonPath))
		{
			OutError = FString::Printf(TEXT("Failed to read upstream result json: %s"), *ResultJsonPath);
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse upstream result json: %s"), *ResultJsonPath);
			return false;
		}

		FString GeneratedAssetPath;
		Root->TryGetStringField(TEXT("generated_asset_path"), GeneratedAssetPath);
		if (GeneratedAssetPath.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Upstream result missing generated_asset_path: %s"), *ResultJsonPath);
			return false;
		}

		OutResolvedSourcePath = GeneratedAssetPath;
		return true;
	}

	bool BuildPenMcpPayloadFromPenFile(const FString& PenFilePath, FString& OutNodesJson, FString& OutVariablesJson, FString& OutError)
	{
		FString PenJson;
		if (!FFileHelper::LoadFileToString(PenJson, *PenFilePath))
		{
			OutError = FString::Printf(TEXT("Failed to read .pen file: %s"), *PenFilePath);
			return false;
		}

		TSharedPtr<FJsonObject> RootObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PenJson);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse .pen JSON: %s"), *PenFilePath);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (!RootObject->TryGetArrayField(TEXT("children"), Children))
		{
			OutError = FString::Printf(TEXT(".pen file missing children array: %s"), *PenFilePath);
			return false;
		}

		const TSharedPtr<FJsonObject>* VariablesObjectPtr = nullptr;
		TSharedPtr<FJsonObject> VariablesObject;
		if (RootObject->TryGetObjectField(TEXT("variables"), VariablesObjectPtr))
		{
			VariablesObject = *VariablesObjectPtr;
		}
		if (!VariablesObject.IsValid())
		{
			VariablesObject = MakeShared<FJsonObject>();
		}

		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutNodesJson);
			if (!FJsonSerializer::Serialize(*Children, Writer))
			{
				OutError = FString::Printf(TEXT("Failed to serialize .pen children payload: %s"), *PenFilePath);
				return false;
			}
		}

		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutVariablesJson);
			if (!FJsonSerializer::Serialize(VariablesObject.ToSharedRef(), Writer))
			{
				OutError = FString::Printf(TEXT("Failed to serialize .pen variables payload: %s"), *PenFilePath);
				return false;
			}
		}

		return true;
	}

	bool LoadFigmaSourceData(
		const FString& SourcePath,
		const FString& TargetNodeId,
		FDesignDocument& OutDocument,
		FImportAnalysisResult& OutAnalysis,
		FString& OutError)
	{
		if (!Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(SourcePath, OutDocument, OutError, TargetNodeId))
		{
			return false;
		}

		DesignFontImportService::PrepareDocumentFonts(OutDocument);
		OutAnalysis = DesignImportPipeline::AnalyzeImport(OutDocument, FImportAnalysisInput{ true });
		return true;
	}

	bool ResolveAutomationRenderSpecRuntimeFields(FAutomationRenderSpec& InOutSpec, FString& OutError)
	{
		FString ResolvedSourcePath;
		if (!ResolveGeneratedAssetSourcePath(InOutSpec.SourcePath, ResolvedSourcePath, OutError))
		{
			return false;
		}

		InOutSpec.SourcePath = MoveTemp(ResolvedSourcePath);
		return true;
	}

	void ResetAutomationTransientState()
	{
		FlushRenderingCommands();
		FlushAsyncLoading();
		FFontResolver::ClearCache();
		FFontResolver::ResetExternalResolvers();
		DesignFontImportService::ResetTransientState();
		FDesignPathRasterizer::ClearCache();
		FProceduralShapeGenerator::ReleaseTransientTextures();
		CollectGarbage(RF_NoFlags, true);
	}

	void PrepareAutomationTransientState(const FAutomationRenderSpec& Spec)
	{
		static FString LastOutputPath;
		if (LastOutputPath.IsEmpty())
		{
			LastOutputPath = Spec.OutputPath;
			return;
		}

		if (LastOutputPath.Equals(Spec.OutputPath, ESearchCase::CaseSensitive))
		{
			return;
		}

		ResetAutomationTransientState();
		LastOutputPath = Spec.OutputPath;
	}

	FString NormalizeWidgetBlueprintObjectPath(const FString& AssetPath)
	{
		FString Normalized = AssetPath.TrimStartAndEnd();
		if (Normalized.IsEmpty())
		{
			return Normalized;
		}

		const FString AssetName = FPaths::GetBaseFilename(Normalized);
		const FString ObjectSuffix = TEXT(".") + AssetName;
		if (!AssetName.IsEmpty() && !Normalized.EndsWith(ObjectSuffix))
		{
			Normalized += ObjectSuffix;
		}
		return Normalized;
	}

	bool RunAutomationRender(const FAutomationRenderSpec& Spec, FAutomationRenderResult& OutResult)
	{
		auto MakeAutomationImportRequest = [&](const bool bResolveImages)
		{
			FDesignImportRequest Request;
			Request.OutputPath = Spec.OutputPath;
			Request.bUsePreview = false;
			Request.bResolveImages = bResolveImages;
			Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintAndCompiledPng;
			Request.Generation.CompiledPngFilePath = Spec.RenderPngPath;
			Request.Generation.CompiledRenderSize = Spec.RenderSize;
			return Request;
		};

		auto ApplyExecutionResult = [&](const FDesignImportExecutionResult& ExecutionResult)
		{
			OutResult.GeneratedAssetPath = ExecutionResult.GeneratedAssetPath;
			OutResult.RenderPngPath = ExecutionResult.RenderPngPath.IsEmpty()
				? Spec.RenderPngPath
				: ExecutionResult.RenderPngPath;
			OutResult.RenderSize = ExecutionResult.RenderedSize;
			return ExecutionResult.bSucceeded;
		};

		auto ApplyGenerationResult = [&](const FArtifactGenerationResult& GenerationResult)
		{
			OutResult.GeneratedAssetPath = GenerationResult.GeneratedAssetPath;
			OutResult.RenderPngPath = GenerationResult.RenderPngPath.IsEmpty()
				? Spec.RenderPngPath
				: GenerationResult.RenderPngPath;
			OutResult.RenderSize = GenerationResult.RenderedSize;
			return GenerationResult.bSucceeded;
		};

		auto CaptureGeneratedAssetStructure = [&]()
		{
			if (!OutResult.GeneratedAssetPath.IsEmpty())
			{
				if (UWidgetBlueprint* GeneratedBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *OutResult.GeneratedAssetPath))
				{
					OutResult.AssetStructure = Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(GeneratedBlueprint->WidgetTree);
				}
			}
		};

		OutResult.Mode = Spec.Mode;
		OutResult.SourcePath = Spec.SourcePath;
		OutResult.RenderPngPath = Spec.RenderPngPath;
		UE_LOG(LogDesign2UMG, Log,
			TEXT("AutomationRender: mode=%s source=%s node=%s output=%s png=%s size=%dx%d"),
			*Spec.Mode, *Spec.SourcePath, *Spec.NodeId, *Spec.OutputPath, *Spec.RenderPngPath,
			Spec.RenderSize.X, Spec.RenderSize.Y);

		UWidgetBlueprint* WidgetBlueprint = nullptr;
		if (Spec.Mode.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
		{
			const FString ObjectPath = NormalizeWidgetBlueprintObjectPath(Spec.SourcePath);
			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: loading asset %s"), *ObjectPath);
			WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
			if (!WidgetBlueprint)
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *ObjectPath);
				return false;
			}

			OutResult.GeneratedAssetPath = WidgetBlueprint->GetPathName();
			OutResult.AssetStructure = Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(WidgetBlueprint->WidgetTree);
			if (!DesignImportUtils::RenderCompiledBlueprintToPngFile(
				WidgetBlueprint,
				Spec.RenderPngPath,
				Spec.RenderSize,
				&OutResult.RenderSize))
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to render compiled Widget Blueprint: %s"), *ObjectPath);
				return false;
			}

			OutResult.bSuccess = true;
			return true;
		}
		if (Spec.Mode.Equals(TEXT("pen"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: importing pen source"));
			FDesignDocument Document;
			FString LoadError;
			if (!DesignImportPipeline::LoadPenDocument(Spec.SourcePath, Spec.NodeId, Document, &LoadError))
			{
				OutResult.ErrorMessage = LoadError;
				return false;
			}

			OutResult.SourceStructure = Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(
				Document,
				Spec.NodeId,
				ShouldSkipDesignerOnlyNodeForAutomationSnapshot);

			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: generating blueprint and compiled png"));
			if (!ApplyExecutionResult(DesignImportPipeline::Execute(Document, MakeAutomationImportRequest(true))))
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
				return false;
			}

			CaptureGeneratedAssetStructure();
			OutResult.bSuccess = true;
			return true;
		}

		FUMGWidgetData Data;
		FDesignDocument SourceDocument;
		TOptional<FDesignDocument> DiagnosticDocument;
		TOptional<FImportAnalysisResult> DeferredAnalysisResult;
		if (Spec.Mode.Equals(TEXT("pen_mcp"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: importing pen source via MCP payload"));
			FString ErrorMessage;
			if (!DesignImportPipeline::LoadPenDocument(Spec.SourcePath, Spec.NodeId, SourceDocument, &ErrorMessage))
			{
				OutResult.ErrorMessage = ErrorMessage;
				return false;
			}

			FString NodesJson;
			FString VariablesJson;
			if (!BuildPenMcpPayloadFromPenFile(Spec.SourcePath, NodesJson, VariablesJson, ErrorMessage))
			{
				OutResult.ErrorMessage = ErrorMessage;
				return false;
			}

			if (!FPenMCPBridge::ConvertFromMCP(NodesJson, VariablesJson, Data, Spec.NodeId))
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to convert MCP payload for %s"), *Spec.SourcePath);
				return false;
			}

			OutResult.SourceStructure = Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(
				SourceDocument,
				Spec.NodeId,
				ShouldSkipDesignerOnlyNodeForAutomationSnapshot);
			const FImportAnalysisResult AnalysisResult = DesignImportPipeline::AnalyzeImport(Data, FImportAnalysisInput{ true });
			Data = AnalysisResult.Data;
			DeferredAnalysisResult = AnalysisResult;
			DiagnosticDocument = SourceDocument;
		}
		else if (Spec.Mode.Equals(TEXT("local_figma"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: importing local figma export"));
			FString ErrorMessage;
			if (!Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(Spec.SourcePath, SourceDocument, ErrorMessage, Spec.NodeId))
			{
				OutResult.ErrorMessage = ErrorMessage;
				return false;
			}

			OutResult.SourceStructure = Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(
				SourceDocument,
				Spec.NodeId,
				ShouldSkipDesignerOnlyNodeForAutomationSnapshot);
			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: generating blueprint and compiled png"));
			if (!ApplyExecutionResult(DesignImportPipeline::Execute(SourceDocument, MakeAutomationImportRequest(true))))
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
				return false;
			}

			CaptureGeneratedAssetStructure();
			OutResult.bSuccess = true;
			return true;
		}
		else if (Spec.Mode.Equals(TEXT("figma_source"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: importing raw figma source json"));
			FString ErrorMessage;
			FImportAnalysisResult AnalysisResult;
			if (!LoadFigmaSourceData(Spec.SourcePath, Spec.NodeId, SourceDocument, AnalysisResult, ErrorMessage))
			{
				OutResult.ErrorMessage = ErrorMessage;
				return false;
			}

			OutResult.SourceStructure = Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(
				SourceDocument,
				Spec.NodeId,
				ShouldSkipDesignerOnlyNodeForAutomationSnapshot);
			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: generating blueprint and compiled png"));
			if (!ApplyExecutionResult(DesignImportPipeline::Execute(SourceDocument, MakeAutomationImportRequest(true))))
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
				return false;
			}

			CaptureGeneratedAssetStructure();
			OutResult.bSuccess = true;
			return true;
		}
		else
		{
			OutResult.ErrorMessage = FString::Printf(TEXT("Unsupported automation render mode: %s"), *Spec.Mode);
			return false;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: generating blueprint and compiled png"));
		FArtifactGenerationRequest GenerationRequest;
		GenerationRequest.OutputPath = Spec.OutputPath;
		GenerationRequest.Generation = MakeAutomationImportRequest(false).Generation;
		if (DiagnosticDocument.IsSet())
		{
			GenerationRequest.DiagnosticDocument = DiagnosticDocument;
		}
		if (DeferredAnalysisResult.IsSet())
		{
			GenerationRequest.RuntimeImageReport = DeferredAnalysisResult->ImageReport;
		}

		if (!ApplyGenerationResult(DesignImportPipeline::GenerateArtifacts(Data, GenerationRequest)))
		{
			OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
			return false;
		}

		CaptureGeneratedAssetStructure();

		OutResult.bSuccess = true;
		return true;
	}
}

namespace Design2UMGAutomationRender
{
	bool IsAutomationRenderActive()
	{
		return GDesign2UMGAutomationRenderActive;
	}

	void HandleConsoleCommand(const TArray<FString>& Args)
	{
		TGuardValue<bool> AutomationGuard(GDesign2UMGAutomationRenderActive, true);
		FAutomationRenderResult Result;
		Result.Mode = TEXT("unknown");
		const bool bRequestExitAfterCompletion = Args.ContainsByPredicate([](const FString& Arg)
		{
			return Arg.Equals(TEXT("--quit"), ESearchCase::IgnoreCase)
				|| Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase);
		});

		if (Args.Num() < 1)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Usage: Design2UMG.AutomationRender <spec.json>"));
			if (bRequestExitAfterCompletion || FApp::IsUnattended())
			{
				FPlatformMisc::RequestExitWithStatus(false, 1);
			}
			return;
		}

		FString SpecPath = Args[0].TrimStartAndEnd();
		SpecPath.TrimQuotesInline();
		SpecPath.RemoveFromEnd(TEXT(";"));
		SpecPath = SpecPath.TrimStartAndEnd();
		UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender command invoked: spec=%s unattended=%d"), *SpecPath, FApp::IsUnattended() ? 1 : 0);
		FAutomationRenderSpec Spec;
		FString LoadError;
		if (!LoadAutomationRenderSpec(SpecPath, Spec, LoadError))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *LoadError);
			Result.ErrorMessage = LoadError;
			WriteAutomationRenderResult(Spec, Result);
			if (bRequestExitAfterCompletion || FApp::IsUnattended())
			{
				FPlatformMisc::RequestExitWithStatus(false, 1);
			}
			return;
		}

		FString ResolveError;
		if (!ResolveAutomationRenderSpecRuntimeFields(Spec, ResolveError))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *ResolveError);
			Result.Mode = Spec.Mode;
			Result.SourcePath = Spec.SourcePath;
			Result.RenderPngPath = Spec.RenderPngPath;
			Result.ErrorMessage = ResolveError;
			WriteAutomationRenderResult(Spec, Result);
			if (bRequestExitAfterCompletion || FApp::IsUnattended())
			{
				FPlatformMisc::RequestExitWithStatus(false, 1);
			}
			return;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: spec loaded successfully"));
		PrepareAutomationTransientState(Spec);
		const bool bOk = RunAutomationRender(Spec, Result);
		WriteAutomationRenderResult(Spec, Result);

		if (!bOk)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Design2UMG.AutomationRender failed: %s"), *Result.ErrorMessage);
		}
		else
		{
			UE_LOG(LogDesign2UMG, Log,
				TEXT("Design2UMG.AutomationRender succeeded: asset=%s png=%s (%dx%d)"),
				*Result.GeneratedAssetPath,
				*Result.RenderPngPath,
				Result.RenderSize.X,
				Result.RenderSize.Y);
		}

		if (bRequestExitAfterCompletion || FApp::IsUnattended())
		{
			FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
		}
	}
}

