#include "DesignImportPipeline.h"

#include "Design2UMGConverter.h"
#include "DesignImageBakeUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignFontImportService.h"
#include "DesignImportDialogs.h"
#include "DesignImportUtils.h"
#include "DesignRemoteImageImporter.h"
#include "FontResolver.h"
#include "PenMCPBridge.h"
#include "PenParser.h"
#include "FigmaParser.h"
#include "UMGJsonParser.h"
#include "UMGSchema.h"

#include "WidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Interfaces/IPluginManager.h"

namespace DesignImportPipeline
{
	namespace
	{
		struct FArtifactGenerationRuntimeResult
		{
			UWidgetBlueprint* GeneratedBlueprint = nullptr;
		};

		void WriteImportDiagnosticReportImpl(
			const FDesignDocument& Document,
			const FUMGWidgetData& Data,
			const FString& ImageReport,
			const FString& RequestedOutputPath,
			const FString& RequestedRenderPngPath,
			const FString& GeneratedAssetPath,
			const FString& RenderPngPath);
		bool ParseWidgetJsonInternal(const FString& JsonString, FUMGWidgetData& OutData, FString* OutError = nullptr);
		void ResolveImagesInternal(FUMGWidgetData& Data, FString* OutReport = nullptr);
		bool ImportPenJsonInternal(const FString& JsonString, FUMGWidgetData& OutData);
		FOnPreviewConfirmed CreatePreviewConfirmedHandler(
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument,
			const FUMGIconFontMappingChanges& IconFontMappingChanges);
		FDesignImportExecutionResult ExecuteWithAnalysis(
			FUMGWidgetData& Data,
			const FImportAnalysisResult& AnalysisResult,
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument);

		void AddUniqueRecommendation(TArray<FString>& InOutRecommendations, const FString& Recommendation)
		{
			if (!Recommendation.IsEmpty())
			{
				InOutRecommendations.AddUnique(Recommendation);
			}
		}

		FString InferSourceOrigin(const FDesignDocument& Document)
		{
			if (const FString* SourceFilePath = Document.Metadata.Find(TEXT("design.sourceFilePath")))
			{
				const FString Extension = FPaths::GetExtension(*SourceFilePath, false).ToLower();
				if (Extension == TEXT("pen"))
				{
					return TEXT("pen_file");
				}
				if (Extension == TEXT("json"))
				{
					return TEXT("local_json");
				}
				return Extension.IsEmpty() ? TEXT("file") : FString::Printf(TEXT("file_%s"), *Extension);
			}

			return TEXT("inline");
		}

		TSharedRef<FJsonObject> MakeNodeDiagnosticEntry(
			const FString& Kind,
			const FString& NodeId,
			const FString& NodeName,
			const FString& SourceType,
			const FString& Reason,
			const FString& Impact)
		{
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("kind"), Kind);
			if (!NodeId.IsEmpty())
			{
				Entry->SetStringField(TEXT("nodeId"), NodeId);
			}
			if (!NodeName.IsEmpty())
			{
				Entry->SetStringField(TEXT("nodeName"), NodeName);
			}
			if (!SourceType.IsEmpty())
			{
				Entry->SetStringField(TEXT("sourceType"), SourceType);
			}
			if (!Reason.IsEmpty())
			{
				Entry->SetStringField(TEXT("reason"), Reason);
			}
			if (!Impact.IsEmpty())
			{
				Entry->SetStringField(TEXT("impact"), Impact);
			}
			return Entry;
		}

		void CollectSkippedDesignerNodes(
			const FDesignDocument& Document,
			TArray<TSharedPtr<FJsonValue>>& OutValues,
			TArray<FString>& InOutRecommendations)
		{
			if (Document.ParseDiagnostics.SkippedDesignerNodes.Num() > 0)
			{
				for (const FDesignSkippedNodeReport& Report : Document.ParseDiagnostics.SkippedDesignerNodes)
				{
					TSharedRef<FJsonObject> Entry = MakeNodeDiagnosticEntry(
						TEXT("skipped_designer_node"),
						Report.NodeId,
						Report.NodeName,
						Report.NodeType,
						Report.Reason,
						TEXT("node skipped from runtime widget tree"));
					OutValues.Add(MakeShared<FJsonValueObject>(Entry));
				}
				if (OutValues.Num() > 0)
				{
					AddUniqueRecommendation(
						InOutRecommendations,
						TEXT("Design-only nodes are skipped by contract; replace them with supported runtime nodes if output is required."));
				}
				return;
			}

		}

		void CollectNodeDegradationsRecursive(
			const TSharedPtr<FDesignNode>& Node,
			TArray<TSharedPtr<FJsonValue>>& OutValues,
			TSet<FString>& InOutSeenKeys,
			TArray<FString>& InOutRecommendations)
		{
			if (!Node.IsValid())
			{
				return;
			}

			auto AddEntryIfNew = [&](const FString& Kind, const FString& Reason, const FString& Impact)
			{
				const FString Key = FString::Printf(TEXT("%s|%s|%s"), *Node->Id, *Kind, *Reason);
				if (InOutSeenKeys.Contains(Key))
				{
					return;
				}

				InOutSeenKeys.Add(Key);
				OutValues.Add(MakeShared<FJsonValueObject>(
					MakeNodeDiagnosticEntry(
						Kind,
						Node->Id,
						Node->Name,
						Node->Source.SourceNodeType,
						Reason,
						Impact)));
			};

			if (Node->Source.SourceNodeType.Equals(TEXT("TEXT_PATH"), ESearchCase::IgnoreCase))
			{
				AddEntryIfNew(
					TEXT("text_path_fallback"),
					TEXT("text-on-path is downgraded to plain text"),
					TEXT("runtime output uses TextBlock instead of path-shaped text"));
				AddUniqueRecommendation(
					InOutRecommendations,
					TEXT("TEXT_PATH is not formally supported; convert path text to regular text if path geometry is required at runtime."));
			}

			if (!Node->Source.VectorRasterFallbackImageRef.IsEmpty())
			{
				TSharedRef<FJsonObject> Entry = MakeNodeDiagnosticEntry(
					TEXT("vector_raster_fallback"),
					Node->Id,
					Node->Name,
					Node->Source.SourceNodeType,
					TEXT("complex vector was rasterized"),
					TEXT("runtime output uses baked image content for the vector node"));
				Entry->SetStringField(TEXT("imageRef"), Node->Source.VectorRasterFallbackImageRef);
				OutValues.Add(MakeShared<FJsonValueObject>(Entry));
				AddUniqueRecommendation(
					InOutRecommendations,
					TEXT("Vector raster fallback is an explicit degradation path; prefer exporter/parser-side simplification before treating it as semantic support."));
			}

			if (Node->Source.RenderFallback.HasAny())
			{
				TSharedRef<FJsonObject> Entry = MakeNodeDiagnosticEntry(
					TEXT("render_fallback"),
					Node->Id,
					Node->Name,
					Node->Source.SourceNodeType,
					Node->Source.RenderFallback.Reason,
					TEXT("runtime output uses a baked render fallback image"));
				if (!Node->Source.RenderFallback.ImageRef.IsEmpty())
				{
					Entry->SetStringField(TEXT("imageRef"), Node->Source.RenderFallback.ImageRef);
				}
				if (!Node->Source.RenderFallback.Scope.IsEmpty())
				{
					Entry->SetStringField(TEXT("scope"), Node->Source.RenderFallback.Scope);
				}
				if (!Node->Source.RenderFallback.Source.IsEmpty())
				{
					Entry->SetStringField(TEXT("source"), Node->Source.RenderFallback.Source);
				}
				OutValues.Add(MakeShared<FJsonValueObject>(Entry));
				AddUniqueRecommendation(
					InOutRecommendations,
					TEXT("Render fallback is a last-resort approximation; fix source/exporter/parser coverage before treating the node as fully supported."));
			}

			for (const TSharedPtr<FDesignNode>& Child : Node->Children)
			{
				CollectNodeDegradationsRecursive(Child, OutValues, InOutSeenKeys, InOutRecommendations);
			}
		}

		FString InferEffectWrapperReason(const FUMGWidgetNode& Widget)
		{
			if (Widget.Type.Equals(TEXT("BackgroundBlur"), ESearchCase::IgnoreCase))
			{
				return TEXT("background_blur");
			}

			if (Widget.Type.Equals(TEXT("RetainerBox"), ESearchCase::IgnoreCase))
			{
				if (Widget.Effect.EffectMaterial.Contains(TEXT("inner_shadow"), ESearchCase::IgnoreCase))
				{
					return TEXT("inner_shadow");
				}
				if (Widget.Effect.EffectMaterial.Contains(TEXT("blur"), ESearchCase::IgnoreCase))
				{
					return TEXT("foreground_blur");
				}
				return Widget.Effect.EffectMaterial.IsEmpty() ? TEXT("retainer_effect") : Widget.Effect.EffectMaterial;
			}

			return FString();
		}

		void CollectEffectWrappersRecursive(
			const FUMGWidgetNode& Widget,
			TArray<TSharedPtr<FJsonValue>>& OutValues,
			TArray<FString>& InOutRecommendations)
		{
			const bool bIsBackgroundBlur = Widget.Type.Equals(TEXT("BackgroundBlur"), ESearchCase::IgnoreCase);
			const bool bIsRetainerBox = Widget.Type.Equals(TEXT("RetainerBox"), ESearchCase::IgnoreCase);
			if (bIsBackgroundBlur || bIsRetainerBox)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("widgetId"), Widget.Id);
				Entry->SetStringField(TEXT("widgetName"), Widget.Name);
				Entry->SetStringField(TEXT("wrapperType"), Widget.Type);
				Entry->SetStringField(TEXT("reason"), InferEffectWrapperReason(Widget));
				Entry->SetNumberField(TEXT("childCount"), Widget.Children.Num());
				if (Widget.Effect.bHasEffectMaterial || !Widget.Effect.EffectMaterial.IsEmpty())
				{
					Entry->SetStringField(TEXT("effectMaterial"), Widget.Effect.EffectMaterial);
				}
				if (Widget.Effect.bHasBlurStrength)
				{
					Entry->SetNumberField(TEXT("blurStrength"), Widget.Effect.BlurStrength);
				}
				OutValues.Add(MakeShared<FJsonValueObject>(Entry));
				AddUniqueRecommendation(
					InOutRecommendations,
					TEXT("Effect wrappers are approximation paths; verify visual cost and trigger reason before widening their use."));
			}

			for (const FUMGWidgetNode& Child : Widget.Children)
			{
				CollectEffectWrappersRecursive(Child, OutValues, InOutRecommendations);
			}
		}

		void CollectImageFailureEntries(
			const FString& ImageReport,
			TArray<TSharedPtr<FJsonValue>>& OutValues,
			TArray<FString>& InOutRecommendations)
		{
			TArray<FString> Lines;
			ImageReport.ParseIntoArrayLines(Lines, true);
			for (const FString& Line : Lines)
			{
				if (!Line.Contains(TEXT("失败")) || Line.StartsWith(TEXT("远程图片导入:"), ESearchCase::CaseSensitive))
				{
					continue;
				}

				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("message"), Line);
				if (Line.Contains(TEXT("下载失败")))
				{
					Entry->SetStringField(TEXT("kind"), TEXT("download_failure"));
				}
				else if (Line.Contains(TEXT("解码失败")))
				{
					Entry->SetStringField(TEXT("kind"), TEXT("decode_failure"));
				}
				else if (Line.Contains(TEXT("导入失败")))
				{
					Entry->SetStringField(TEXT("kind"), TEXT("import_failure"));
				}
				else
				{
					Entry->SetStringField(TEXT("kind"), TEXT("resource_failure"));
				}
				OutValues.Add(MakeShared<FJsonValueObject>(Entry));
			}

			if (OutValues.Num() > 0)
			{
				AddUniqueRecommendation(
					InOutRecommendations,
					TEXT("Remote resource failures should be fixed at the asset source or replaced with local assets before final release verification."));
			}
		}

		void PrepareDocumentForGeneration(FDesignDocument& Document, FUMGWidgetData& OutPreparedData)
		{
			DesignFontImportService::PrepareDocumentFonts(Document);
			DesignImageBakeUtils::BakeDocumentLocalImageBlendOverlays(
				Document,
				FString(),
				TEXT("DesignImportPipeline"));
			OutPreparedData = FDesign2UMGConverter::Convert(Document);
		}

		FString BuildImportReportPath(const FDesignDocument& Document)
		{
			const FString* SourceFilePath = Document.Metadata.Find(TEXT("design.sourceFilePath"));
			const FString SourceLabel = SourceFilePath
				? FPaths::GetBaseFilename(*SourceFilePath)
				: TEXT("inline");
			const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
			const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
			return FPaths::Combine(
				FPaths::ProjectSavedDir(),
				TEXT("Design2UMG"),
				TEXT("ImportReports"),
				FString::Printf(TEXT("%s_%s_%s.import_report.json"), *Timestamp, *SourceLabel, *UniqueSuffix));
		}

		FString ResolvePluginVersionName()
		{
			if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG")))
			{
				return Plugin->GetDescriptor().VersionName;
			}
			return TEXT("unknown");
		}

		void WriteImportDiagnosticReportImpl(
			const FDesignDocument& Document,
			const FUMGWidgetData& Data,
			const FString& ImageReport,
			const FString& RequestedOutputPath,
			const FString& RequestedRenderPngPath,
			const FString& GeneratedAssetPath,
			const FString& RenderPngPath)
		{
			TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetStringField(TEXT("pluginVersion"), ResolvePluginVersionName());
			Root->SetStringField(TEXT("documentVersion"), Document.Version);

			TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
			Source->SetStringField(TEXT("origin"), InferSourceOrigin(Document));
			if (!Document.Version.IsEmpty())
			{
				Source->SetStringField(TEXT("documentVersion"), Document.Version);
			}

			if (const FString* SourceFilePath = Document.Metadata.Find(TEXT("design.sourceFilePath")))
			{
				Root->SetStringField(TEXT("sourceFilePath"), *SourceFilePath);
				const FString SourceIdentity = DesignImageBakeUtils::BuildSourceDocumentIdentity(*SourceFilePath);
				Root->SetStringField(TEXT("sourceIdentity"), SourceIdentity);
				Source->SetStringField(TEXT("filePath"), *SourceFilePath);
				Source->SetStringField(TEXT("identity"), SourceIdentity);
			}

			TSharedRef<FJsonObject> Output = MakeShared<FJsonObject>();
			if (!RequestedOutputPath.IsEmpty())
			{
				Root->SetStringField(TEXT("requestedOutputPath"), RequestedOutputPath);
				Output->SetStringField(TEXT("requestedOutputPath"), RequestedOutputPath);
			}
			if (!RequestedRenderPngPath.IsEmpty())
			{
				Root->SetStringField(TEXT("requestedRenderPngPath"), RequestedRenderPngPath);
				Output->SetStringField(TEXT("requestedRenderPngPath"), RequestedRenderPngPath);
			}
			if (!GeneratedAssetPath.IsEmpty())
			{
				Root->SetStringField(TEXT("generatedAssetPath"), GeneratedAssetPath);
				Output->SetStringField(TEXT("generatedAssetPath"), GeneratedAssetPath);
			}
			if (!RenderPngPath.IsEmpty())
			{
				Root->SetStringField(TEXT("renderPngPath"), RenderPngPath);
				Output->SetStringField(TEXT("renderPngPath"), RenderPngPath);
			}

			TArray<FString> Recommendations;
			TArray<TSharedPtr<FJsonValue>> SkippedNodeValues;
			CollectSkippedDesignerNodes(Document, SkippedNodeValues, Recommendations);
			Root->SetArrayField(TEXT("skippedDesignerNodes"), SkippedNodeValues);

			TArray<TSharedPtr<FJsonValue>> WarningValues;
			for (const FString& Warning : Data.Warnings)
			{
				WarningValues.Add(MakeShared<FJsonValueString>(Warning));
			}
			Root->SetArrayField(TEXT("warnings"), WarningValues);

			TArray<TSharedPtr<FJsonValue>> MissingFontValues;
			for (const FString& FontFamily : FFontResolver::FindMissingFonts(Document))
			{
				MissingFontValues.Add(MakeShared<FJsonValueString>(FontFamily));
			}
			Root->SetArrayField(TEXT("missingFonts"), MissingFontValues);
			if (MissingFontValues.Num() > 0)
			{
				AddUniqueRecommendation(
					Recommendations,
					TEXT("Map required fonts locally or keep font auto-import optional; missing fonts should not be treated as an online-only runtime dependency."));
			}

			if (!ImageReport.IsEmpty())
			{
				Root->SetStringField(TEXT("imageImportReport"), ImageReport);
			}

			TArray<TSharedPtr<FJsonValue>> DegradationValues;
			TSet<FString> SeenDegradationKeys;
			for (const TSharedPtr<FDesignNode>& RootNode : Document.Children)
			{
				CollectNodeDegradationsRecursive(RootNode, DegradationValues, SeenDegradationKeys, Recommendations);
			}

			TArray<TSharedPtr<FJsonValue>> EffectWrapperValues;
			CollectEffectWrappersRecursive(Data.RootWidget, EffectWrapperValues, Recommendations);

			TArray<TSharedPtr<FJsonValue>> ImageFailureValues;
			CollectImageFailureEntries(ImageReport, ImageFailureValues, Recommendations);

			TSharedRef<FJsonObject> Diagnostics = MakeShared<FJsonObject>();
			Diagnostics->SetArrayField(TEXT("warnings"), WarningValues);
			Diagnostics->SetArrayField(TEXT("skippedDesignerNodes"), SkippedNodeValues);
			Diagnostics->SetArrayField(TEXT("degradations"), DegradationValues);

			TSharedRef<FJsonObject> Resources = MakeShared<FJsonObject>();
			Resources->SetArrayField(TEXT("missingFonts"), MissingFontValues);
			Resources->SetArrayField(TEXT("imageFailures"), ImageFailureValues);
			if (!ImageReport.IsEmpty())
			{
				Resources->SetStringField(TEXT("imageImportSummary"), ImageReport);
			}

			TSharedRef<FJsonObject> Effects = MakeShared<FJsonObject>();
			Effects->SetNumberField(TEXT("wrapperCount"), EffectWrapperValues.Num());
			Effects->SetArrayField(TEXT("wrappers"), EffectWrapperValues);

			TArray<TSharedPtr<FJsonValue>> RecommendationValues;
			for (const FString& Recommendation : Recommendations)
			{
				RecommendationValues.Add(MakeShared<FJsonValueString>(Recommendation));
			}

			Root->SetObjectField(TEXT("source"), Source);
			Root->SetObjectField(TEXT("output"), Output);
			Root->SetObjectField(TEXT("diagnostics"), Diagnostics);
			Root->SetObjectField(TEXT("resources"), Resources);
			Root->SetObjectField(TEXT("effects"), Effects);
			Root->SetArrayField(TEXT("recommendations"), RecommendationValues);

			const FString ReportPath = BuildImportReportPath(Document);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
			FString Payload;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
			FJsonSerializer::Serialize(Root, Writer);
			FFileHelper::SaveStringToFile(Payload, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8);
			UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportPipeline: wrote import diagnostic report %s"), *ReportPath);
		}

		void MaybeWriteImportDiagnosticReport(
			const FArtifactGenerationRequest& Request,
			const FUMGWidgetData& Data,
			const FArtifactGenerationResult& Result)
		{
			if (!Request.DiagnosticDocument.IsSet() || (!Result.bSucceeded && Result.GeneratedAssetPath.IsEmpty()))
			{
				return;
			}

			WriteImportDiagnosticReportImpl(
				Request.DiagnosticDocument.GetValue(),
				Data,
				Request.DiagnosticImageReportOverride.IsEmpty() ? Request.RuntimeImageReport : Request.DiagnosticImageReportOverride,
				Request.OutputPath,
				Result.RenderPngPath.IsEmpty() ? Request.Generation.CompiledPngFilePath : Result.RenderPngPath,
				Result.GeneratedAssetPath,
				Result.RenderPngPath);
		}

		FArtifactGenerationResult GenerateArtifactsInternal(
			FUMGWidgetData& Data,
			const FArtifactGenerationRequest& Request,
			FArtifactGenerationRuntimeResult* OutRuntimeResult = nullptr)
		{
			FArtifactGenerationResult Result;
			switch (Request.Generation.Mode)
			{
			case EDesignBlueprintGenerationMode::BlueprintOnly:
			{
				UWidgetBlueprint* GeneratedBlueprint = DesignImportUtils::GenerateBlueprint(Data, Request.OutputPath);
				Result.bSucceeded = (GeneratedBlueprint != nullptr);
				if (GeneratedBlueprint)
				{
					Result.GeneratedAssetPath = GeneratedBlueprint->GetPathName();
				}
				if (OutRuntimeResult)
				{
					OutRuntimeResult->GeneratedBlueprint = GeneratedBlueprint;
				}
				break;
			}

			case EDesignBlueprintGenerationMode::BlueprintAndPng:
				Result.bSucceeded = DesignImportUtils::GenerateBlueprintAndPng(
					Data,
					Request.OutputPath,
					&Result.GeneratedAssetPath,
					&Result.RenderPngPath);
				break;

			case EDesignBlueprintGenerationMode::BlueprintAndCompiledPng:
				Result.bSucceeded = DesignImportUtils::GenerateBlueprintAndCompiledPng(
					Data,
					Request.OutputPath,
					Request.Generation.CompiledPngFilePath,
					Request.Generation.CompiledRenderSize,
					&Result.GeneratedAssetPath,
					&Result.RenderedSize);
				Result.RenderPngPath = Request.Generation.CompiledPngFilePath;
				break;
			}

			MaybeWriteImportDiagnosticReport(Request, Data, Result);
			return Result;
		}

		bool ParseWidgetJsonInternal(const FString& JsonString, FUMGWidgetData& OutData, FString* OutError)
		{
			FString ParseError;
			if (FUMGJsonParser::ParseJson(JsonString, OutData, &ParseError))
			{
				return true;
			}

			if (OutError)
			{
				*OutError = MoveTemp(ParseError);
			}
			return false;
		}

		void ResolveImagesInternal(FUMGWidgetData& Data, FString* OutReport)
		{
			FString Report;
			const bool bOk = FDesignRemoteImageImporter::ResolveRemoteImages(Data, &Report);
			if (OutReport)
			{
				*OutReport = Report;
			}
			if (!Report.IsEmpty())
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("%s"), *Report);
			}
			if (!bOk)
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportPipeline: Unresolved remote images; generation will continue"));
				FDesign2UMGNotification::Warning(NSLOCTEXT("DesignImportPipeline", "ImageWarning", "Design2UMG: Some images failed to download"));
			}
		}

		bool ImportPenJsonInternal(const FString& JsonString, FUMGWidgetData& OutData)
		{
			FDesignDocument Document;
			if (!FPenParser::ParseDocument(JsonString, Document))
			{
				return false;
			}

			OutData = AnalyzeImport(Document, FImportAnalysisInput{false}).Data;
			return true;
		}

		FOnPreviewConfirmed CreatePreviewConfirmedHandler(
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument,
			const FUMGIconFontMappingChanges& IconFontMappingChanges)
		{
			return FOnPreviewConfirmed::CreateLambda(
				[CapturedIconFontMappingChanges = IconFontMappingChanges,
				 CapturedGeneration = Request.Generation,
				 bCapturedResolveImages = Request.bResolveImages,
				 CapturedDiagnosticDocument = DiagnosticDocument](const FPreviewGenerationRequest& PreviewGenerationRequest)
				{
					FPreviewApprovedSnapshot ApprovedSnapshot = PreviewGenerationRequest.ApprovedSnapshot;
					ApprovedSnapshot.Data.IconFontMappingChanges = CapturedIconFontMappingChanges;

					FArtifactGenerationRequest GenerationRequest = PreviewGenerationRequest.GenerationRequest;
					GenerationRequest.Generation = CapturedGeneration;
					GenerationRequest.bResolveImages = bCapturedResolveImages;
					if (CapturedDiagnosticDocument.IsSet())
					{
						GenerationRequest.DiagnosticDocument = CapturedDiagnosticDocument;
					}

					GenerateArtifacts(ApprovedSnapshot, GenerationRequest);
				});
		}

		FDesignImportExecutionResult ExecuteWithAnalysis(
			FUMGWidgetData& Data,
			const FImportAnalysisResult& AnalysisResult,
			const FDesignImportRequest& Request,
			const TOptional<FDesignDocument>& DiagnosticDocument)
		{
			FDesignImportExecutionResult Result;
			if (Request.bUsePreview
				&& DesignImportDialogs::OpenPreviewWindow(
					Data,
					Request.OutputPath,
					Request.PreviewWindowTitle,
					CreatePreviewConfirmedHandler(Request, DiagnosticDocument, AnalysisResult.Data.IconFontMappingChanges),
					FOnPreviewCancelled::CreateLambda(
						[]()
						{
							UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportPipeline: Preview cancelled"));
						})))
			{
				Result.bSucceeded = true;
				Result.bPreviewOpened = true;
				return Result;
			}

			FArtifactGenerationRequest GenerationRequest;
			GenerationRequest.OutputPath = Request.OutputPath;
			GenerationRequest.Generation = Request.Generation;
			GenerationRequest.RuntimeImageReport = AnalysisResult.ImageReport;
			GenerationRequest.DiagnosticImageReportOverride = Request.DiagnosticImageReport;
			if (DiagnosticDocument.IsSet())
			{
				GenerationRequest.DiagnosticDocument = DiagnosticDocument;
			}
			else if (Request.DiagnosticDocument.IsSet())
			{
				GenerationRequest.DiagnosticDocument = Request.DiagnosticDocument;
			}

			FArtifactGenerationRuntimeResult RuntimeResult;
			const FArtifactGenerationResult GenerationResult = GenerateArtifactsInternal(Data, GenerationRequest, &RuntimeResult);
			Result.bSucceeded = GenerationResult.bSucceeded;
			Result.GeneratedBlueprint = RuntimeResult.GeneratedBlueprint;
			Result.GeneratedAssetPath = GenerationResult.GeneratedAssetPath;
			Result.RenderPngPath = GenerationResult.RenderPngPath;
			Result.RenderedSize = GenerationResult.RenderedSize;
			return Result;
		}
	}

	FImportAnalysisResult AnalyzeImport(FUMGWidgetData& Data, const FImportAnalysisInput& Input)
	{
		FImportAnalysisResult Result;
		if (Input.bResolveImages)
		{
			ResolveImagesInternal(Data, &Result.ImageReport);
		}
		Result.Data = Data;
		return Result;
	}

	FImportAnalysisResult AnalyzeImport(FDesignDocument& Document, const FImportAnalysisInput& Input)
	{
		FImportAnalysisResult Result;
		PrepareDocumentForGeneration(Document, Result.Data);
		if (Input.bResolveImages)
		{
			ResolveImagesInternal(Result.Data, &Result.ImageReport);
		}
		return Result;
	}

	FPreviewRefreshResult AnalyzePreviewRefresh(
		const FPreviewRefreshInput& Input,
		const FUMGIconFontMappingChanges* MappingChanges)
	{
		FPreviewRefreshResult Result;
		Result.RequestId = Input.RequestId;
		Result.EditedJson = Input.EditedJson;
		if (!ParseWidgetJsonInternal(Input.EditedJson, Result.Data, &Result.ParseError))
		{
			return Result;
		}

		if (MappingChanges)
		{
			Result.Data.IconFontMappingChanges = *MappingChanges;
		}

		if (Input.bResolveImages)
		{
			ResolveImagesInternal(Result.Data, &Result.ImageReport);
		}

		Result.bSucceeded = true;
		return Result;
	}

	FArtifactGenerationResult GenerateArtifacts(FUMGWidgetData& Data, const FArtifactGenerationRequest& Request)
	{
		return GenerateArtifactsInternal(Data, Request);
	}

	FArtifactGenerationResult GenerateArtifacts(const FPreviewApprovedSnapshot& ApprovedSnapshot, const FArtifactGenerationRequest& Request)
	{
		FArtifactGenerationRequest GenerationRequest = Request;
		if (GenerationRequest.SourceRequestId == 0)
		{
			GenerationRequest.SourceRequestId = ApprovedSnapshot.RequestId;
		}
		if (GenerationRequest.RuntimeImageReport.IsEmpty())
		{
			GenerationRequest.RuntimeImageReport = ApprovedSnapshot.ImageReport;
		}

		FUMGWidgetData GenerationData = ApprovedSnapshot.Data;
		if (GenerationRequest.bResolveImages)
		{
			FString ResolvedImageReport;
			ResolveImagesInternal(GenerationData, &ResolvedImageReport);
			GenerationRequest.RuntimeImageReport = ResolvedImageReport;
		}
		return GenerateArtifactsInternal(GenerationData, GenerationRequest);
	}

	FDesignImportExecutionResult Execute(FUMGWidgetData& Data, const FDesignImportRequest& Request)
	{
		const FImportAnalysisResult AnalysisResult = AnalyzeImport(Data, FImportAnalysisInput{Request.bResolveImages});
		return ExecuteWithAnalysis(Data, AnalysisResult, Request, Request.DiagnosticDocument);
	}

	FDesignImportExecutionResult Execute(FDesignDocument& Document, const FDesignImportRequest& Request)
	{
		const FImportAnalysisResult AnalysisResult = AnalyzeImport(Document, FImportAnalysisInput{Request.bResolveImages});
		FUMGWidgetData Data = AnalysisResult.Data;
		FDesignImportRequest FinalRequest = Request;
		FinalRequest.DiagnosticImageReport = AnalysisResult.ImageReport;
		return ExecuteWithAnalysis(Data, AnalysisResult, FinalRequest, TOptional<FDesignDocument>(Document));
	}

	void WriteImportDiagnosticReport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& ImageReport,
		const FString& RequestedOutputPath,
		const FString& RequestedRenderPngPath,
		const FString& GeneratedAssetPath,
		const FString& RenderPngPath)
	{
		WriteImportDiagnosticReportImpl(
			Document,
			Data,
			ImageReport,
			RequestedOutputPath,
			RequestedRenderPngPath,
			GeneratedAssetPath,
			RenderPngPath);
	}

	bool LoadPenDocument(
		const FString& FilePath,
		const FString& TargetNodeId,
		FDesignDocument& OutDocument,
		FString* OutError)
	{
		auto Fail = [OutError](const FString& Message)
		{
			if (OutError)
			{
				*OutError = Message;
			}
			return false;
		};

		if (TargetNodeId.IsEmpty())
		{
			if (FPenParser::ParseFromFile(FilePath, OutDocument))
			{
				OutDocument.Metadata.Add(TEXT("design.sourceFilePath"), FPaths::ConvertRelativePathToFull(FilePath));
				return true;
			}

			return Fail(FString::Printf(TEXT("Failed to parse pen source: %s"), *FilePath));
		}

		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			return Fail(FString::Printf(TEXT("Failed to read pen source: %s"), *FilePath));
		}

		TSharedPtr<FJsonObject> RootObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, RootObject) || !RootObject.IsValid())
		{
			return Fail(FString::Printf(TEXT("Failed to parse pen source JSON: %s"), *FilePath));
		}

		TSharedPtr<FJsonObject> FilteredDocumentObject;
		if (!FPenMCPBridge::BuildFilteredDocumentObject(RootObject, FilteredDocumentObject, TargetNodeId))
		{
			return Fail(FString::Printf(TEXT("Failed to filter pen source for target node '%s'"), *TargetNodeId));
		}

		if (!FPenParser::ParseDocumentObject(FilteredDocumentObject, OutDocument))
		{
			return Fail(FString::Printf(TEXT("Failed to parse filtered pen source: %s"), *FilePath));
		}

		OutDocument.Metadata.Add(TEXT("design.sourceFilePath"), FPaths::ConvertRelativePathToFull(FilePath));
		return true;
	}

	FDesignImportExecutionResult ExecutePenFile(
		const FString& FilePath,
		const FString& TargetNodeId,
		const FDesignImportRequest& Request)
	{
		FDesignDocument Document;
		if (!LoadPenDocument(FilePath, TargetNodeId, Document, nullptr))
		{
			return FDesignImportExecutionResult();
		}

		return Execute(Document, Request);
	}

	bool LoadPenFrames(const FString& FilePath, TArray<FDesignFrameInfo>& OutFrames)
	{
		FDesignDocument Document;
		if (!LoadPenDocument(FilePath, FString(), Document, nullptr))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportPipeline: Failed to parse %s"), *FilePath);
			return false;
		}

		OutFrames = ExtractTopFrames(Document);
		return true;
	}
}
