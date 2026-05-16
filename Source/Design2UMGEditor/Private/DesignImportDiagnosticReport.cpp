// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportDiagnosticReport.h"

#include "Design2UMGLog.h"
#include "DesignImageBakeUtils.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "FontResolver.h"
#include "UMGSchema.h"

#include "Dom/JsonObject.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace DesignImportDiagnosticReport
{
	namespace
	{
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
			if (Document.ParseDiagnostics().SkippedDesignerNodes.Num() > 0)
			{
				for (const FDesignSkippedNodeReport& Report : Document.ParseDiagnostics().SkippedDesignerNodes)
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
						Node->Source().SourceNodeType,
						Reason,
						Impact)));
			};

			if (Node->Source().SourceNodeType.Equals(TEXT("TEXT_PATH"), ESearchCase::IgnoreCase))
			{
				AddEntryIfNew(
					TEXT("text_path_fallback"),
					TEXT("text-on-path is downgraded to plain text"),
					TEXT("runtime output uses TextBlock instead of path-shaped text"));
				AddUniqueRecommendation(
					InOutRecommendations,
					TEXT("TEXT_PATH is not formally supported; convert path text to regular text if path geometry is required at runtime."));
			}

			if (!Node->Source().VectorRasterFallbackImageRef.IsEmpty())
			{
				TSharedRef<FJsonObject> Entry = MakeNodeDiagnosticEntry(
					TEXT("vector_raster_fallback"),
					Node->Id,
					Node->Name,
					Node->Source().SourceNodeType,
					TEXT("complex vector was rasterized"),
					TEXT("runtime output uses baked image content for the vector node"));
				Entry->SetStringField(TEXT("imageRef"), Node->Source().VectorRasterFallbackImageRef);
				OutValues.Add(MakeShared<FJsonValueObject>(Entry));
				AddUniqueRecommendation(
					InOutRecommendations,
					TEXT("Vector raster fallback is an explicit degradation path; prefer exporter/parser-side simplification before treating it as semantic support."));
			}

			if (Node->Source().RenderFallback.HasAny())
			{
				TSharedRef<FJsonObject> Entry = MakeNodeDiagnosticEntry(
					TEXT("render_fallback"),
					Node->Id,
					Node->Name,
					Node->Source().SourceNodeType,
					Node->Source().RenderFallback.Reason,
					TEXT("runtime output uses a baked render fallback image"));
				if (!Node->Source().RenderFallback.ImageRef.IsEmpty())
				{
					Entry->SetStringField(TEXT("imageRef"), Node->Source().RenderFallback.ImageRef);
				}
				if (!Node->Source().RenderFallback.Scope.IsEmpty())
				{
					Entry->SetStringField(TEXT("scope"), Node->Source().RenderFallback.Scope);
				}
				if (!Node->Source().RenderFallback.Source.IsEmpty())
				{
					Entry->SetStringField(TEXT("source"), Node->Source().RenderFallback.Source);
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
				const FString EffectMaterial = Widget.Effect.EffectMaterial.Get(FString());
				if (EffectMaterial.Contains(TEXT("inner_shadow"), ESearchCase::IgnoreCase))
				{
					return TEXT("inner_shadow");
				}
				if (EffectMaterial.Contains(TEXT("blur"), ESearchCase::IgnoreCase))
				{
					return TEXT("foreground_blur");
				}
				return EffectMaterial.IsEmpty() ? TEXT("retainer_effect") : EffectMaterial;
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
				if (Widget.Effect.EffectMaterial.IsSet())
				{
					Entry->SetStringField(TEXT("effectMaterial"), Widget.Effect.EffectMaterial.GetValue());
				}
				if (Widget.Effect.BlurStrength.IsSet())
				{
					Entry->SetNumberField(TEXT("blurStrength"), Widget.Effect.BlurStrength.GetValue());
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
				if (!Line.Contains(TEXT("failed"), ESearchCase::IgnoreCase)
					|| Line.StartsWith(TEXT("Remote image import:"), ESearchCase::CaseSensitive))
				{
					continue;
				}

				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("message"), Line);
				if (Line.Contains(TEXT("download failed"), ESearchCase::IgnoreCase))
				{
					Entry->SetStringField(TEXT("kind"), TEXT("download_failure"));
				}
				else if (Line.Contains(TEXT("decode failed"), ESearchCase::IgnoreCase))
				{
					Entry->SetStringField(TEXT("kind"), TEXT("decode_failure"));
				}
				else if (Line.Contains(TEXT("import failed"), ESearchCase::IgnoreCase))
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
	}

	void Write(
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
}
