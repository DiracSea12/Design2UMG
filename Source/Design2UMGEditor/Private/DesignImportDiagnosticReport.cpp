// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportDiagnosticReport.h"

#include "Design2UMGComponentMappingConfig.h"
#include "Design2UMGLog.h"
#include "UMGWidgetTypes.h"
#include "DesignImageBakeUtils.h"
#include "DesignImportUtils.h"
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
		const TCHAR* CommitStatusName(EDesign2UMGCommitStatus Status)
		{
			switch (Status)
			{
			case EDesign2UMGCommitStatus::Succeeded: return TEXT("succeeded");
			case EDesign2UMGCommitStatus::SucceededWithDegradation: return TEXT("succeeded_with_degradation");
			case EDesign2UMGCommitStatus::NotCommitted: return TEXT("not_committed");
			case EDesign2UMGCommitStatus::CommittedWithMetadataFailure: return TEXT("committed_with_metadata_failure");
			case EDesign2UMGCommitStatus::RollbackIncomplete: return TEXT("rollback_incomplete");
			case EDesign2UMGCommitStatus::Conflict: return TEXT("conflict");
			}
			return TEXT("unknown");
		}

		const TCHAR* FileStateName(EDesign2UMGFileState State)
		{
			switch (State)
			{
			case EDesign2UMGFileState::Unchanged: return TEXT("unchanged");
			case EDesign2UMGFileState::Saved: return TEXT("saved");
			case EDesign2UMGFileState::Restored: return TEXT("restored");
			case EDesign2UMGFileState::Indeterminate: return TEXT("indeterminate");
			}
			return TEXT("unknown");
		}

		const TCHAR* MemoryStateName(EDesign2UMGMemoryState State)
		{
			switch (State)
			{
			case EDesign2UMGMemoryState::Unchanged: return TEXT("unchanged");
			case EDesign2UMGMemoryState::Modified: return TEXT("modified");
			case EDesign2UMGMemoryState::Restored: return TEXT("restored");
			case EDesign2UMGMemoryState::RecoveryFailed: return TEXT("recovery_failed");
			}
			return TEXT("unknown");
		}

		FString ResolveDocumentSourceFilePath(const FDesignDocument& Document)
		{
			if (!Document.SourceDocument().SourceFilePath.IsEmpty())
			{
				return Document.SourceDocument().SourceFilePath;
			}

			if (const FString* SourceFilePath = Document.Metadata.Find(TEXT("design.sourceFilePath")))
			{
				return *SourceFilePath;
			}

			return FString();
		}

		void AddUniqueRecommendation(TArray<FString>& InOutRecommendations, const FString& Recommendation)
		{
			if (!Recommendation.IsEmpty())
			{
				InOutRecommendations.AddUnique(Recommendation);
			}
		}

		FString InferSourceOrigin(const FDesignDocument& Document)
		{
			const FString SourceFilePath = ResolveDocumentSourceFilePath(Document);
			if (!SourceFilePath.IsEmpty())
			{
				const FString Extension = FPaths::GetExtension(SourceFilePath, false).ToLower();
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

			if (!Node->Visual.VectorRasterFallbackImageRef.IsEmpty())
			{
				TSharedRef<FJsonObject> Entry = MakeNodeDiagnosticEntry(
					TEXT("vector_raster_fallback"),
					Node->Id,
					Node->Name,
					Node->Source().SourceNodeType,
					TEXT("complex vector was rasterized"),
					TEXT("runtime output uses baked image content for the vector node"));
				Entry->SetStringField(TEXT("imageRef"), Node->Visual.VectorRasterFallbackImageRef);
				OutValues.Add(MakeShared<FJsonValueObject>(Entry));
				AddUniqueRecommendation(
					InOutRecommendations,
					TEXT("Vector raster fallback is an explicit degradation path; prefer exporter/parser-side simplification before treating it as semantic support."));
			}

			const FString RenderFallbackReason = Node->Metadata.FindRef(TEXT("design2umgRenderFallbackReason"));
			const FString RenderFallbackScope = Node->Metadata.FindRef(TEXT("design2umgRenderFallbackScope"));
			const FString RenderFallbackSource = Node->Metadata.FindRef(TEXT("design2umgRenderFallbackSource"));
			if (Node->Visual.RenderFallback.HasAny()
				|| !RenderFallbackReason.IsEmpty()
				|| !RenderFallbackScope.IsEmpty()
				|| !RenderFallbackSource.IsEmpty())
			{
				TSharedRef<FJsonObject> Entry = MakeNodeDiagnosticEntry(
					TEXT("render_fallback"),
					Node->Id,
					Node->Name,
					Node->Source().SourceNodeType,
					RenderFallbackReason,
					TEXT("runtime output uses a baked render fallback image"));
				if (!Node->Visual.RenderFallback.ImageRef.IsEmpty())
				{
					Entry->SetStringField(TEXT("imageRef"), Node->Visual.RenderFallback.ImageRef);
				}
				if (!RenderFallbackScope.IsEmpty())
				{
					Entry->SetStringField(TEXT("scope"), RenderFallbackScope);
				}
				if (!RenderFallbackSource.IsEmpty())
				{
					Entry->SetStringField(TEXT("source"), RenderFallbackSource);
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
			if (Widget.Type.Equals(UMGWidgetTypes::BackgroundBlur, ESearchCase::IgnoreCase))
			{
				return TEXT("background_blur");
			}

			if (Widget.Type.Equals(UMGWidgetTypes::RetainerBox, ESearchCase::IgnoreCase))
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
			const bool bIsBackgroundBlur = Widget.Type.Equals(UMGWidgetTypes::BackgroundBlur, ESearchCase::IgnoreCase);
			const bool bIsRetainerBox = Widget.Type.Equals(UMGWidgetTypes::RetainerBox, ESearchCase::IgnoreCase);
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
			const FString SourceFilePath = ResolveDocumentSourceFilePath(Document);
			const FString SourceLabel = !SourceFilePath.IsEmpty()
				? FPaths::GetBaseFilename(SourceFilePath)
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

		bool IsUnsupportedFontSource(const FString& Source)
		{
			const FString Trimmed = Source.TrimStartAndEnd();
			if (Trimmed.IsEmpty())
			{
				return false;
			}

			if (DesignImportUtils::IsHttpUrl(Trimmed))
			{
				return true;
			}

			const FString Extension = FPaths::GetExtension(Trimmed, false).ToLower();
			return Extension == TEXT("ttf")
				|| Extension == TEXT("otf")
				|| Extension == TEXT("ttc")
				|| Extension == TEXT("otc")
				|| Extension == TEXT("woff")
				|| Extension == TEXT("woff2");
		}

		bool IsFontUrlSource(const FString& Source)
		{
			return DesignImportUtils::IsHttpUrl(Source.TrimStartAndEnd());
		}

		bool IsUnsupportedFontFileSource(const FString& Source)
		{
			const FString Trimmed = Source.TrimStartAndEnd();
			if (Trimmed.IsEmpty() || IsFontUrlSource(Trimmed))
			{
				return false;
			}

			return IsUnsupportedFontSource(Trimmed);
		}

		struct FFontResolutionEntryKey
		{
			FString EntryScope = TEXT("text-node");
			FString NodeId;
			FString NodePath;
			TOptional<int32> RunId;
			TOptional<int32> DocumentFontIndex;
		};

		FString BuildFontResolutionInstanceKey(const FFontResolutionEntryKey& EntryKey)
		{
			if (EntryKey.EntryScope == TEXT("document-font-clue"))
			{
				return FString::Printf(
					TEXT("document-font-clue|%d"),
					EntryKey.DocumentFontIndex.GetValue());
			}
			const FString NodeIdentity = EntryKey.NodePath.IsEmpty() ? EntryKey.NodeId : EntryKey.NodePath;
			if (EntryKey.RunId.IsSet())
			{
				return FString::Printf(
					TEXT("text-run|%s|%d"),
					*NodeIdentity,
					EntryKey.RunId.GetValue());
			}
			return FString::Printf(TEXT("text-node|%s"), *NodeIdentity);
		}

		void AddFontResolutionResult(
			TArray<TSharedPtr<FJsonValue>>& OutValues,
			TSet<FString>& InOutSeenKeys,
			const FFontResolutionEntryKey& EntryKey,
			const FString& Family,
			const FString& PostScriptName,
			const FString& Weight,
			const FString& Style,
			const bool bUnsupportedInput,
			const bool bFontUrlIgnored)
		{
			const FString InstanceKey = BuildFontResolutionInstanceKey(EntryKey);
			if (InOutSeenKeys.Contains(InstanceKey))
			{
				return;
			}
			InOutSeenKeys.Add(InstanceKey);

			const FFontResolutionDiagnostic Diagnostic = FFontResolver::ResolveFontDiagnostic(
				Family,
				PostScriptName,
				Weight,
				Style,
				bUnsupportedInput,
				bFontUrlIgnored);

			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("entryScope"), EntryKey.EntryScope);
			if (!EntryKey.NodeId.IsEmpty())
			{
				Entry->SetStringField(TEXT("nodeId"), EntryKey.NodeId);
			}
			if (!EntryKey.NodePath.IsEmpty())
			{
				Entry->SetStringField(TEXT("nodePath"), EntryKey.NodePath);
			}
			if (EntryKey.RunId.IsSet())
			{
				Entry->SetNumberField(TEXT("runId"), EntryKey.RunId.GetValue());
			}
			if (EntryKey.DocumentFontIndex.IsSet())
			{
				Entry->SetNumberField(TEXT("documentFontIndex"), EntryKey.DocumentFontIndex.GetValue());
			}
			Entry->SetStringField(TEXT("declarationKey"), Diagnostic.DeclarationKey);
			Entry->SetStringField(TEXT("declaredFamily"), Diagnostic.DeclaredFamily);
			Entry->SetStringField(TEXT("declaredPostScriptName"), Diagnostic.DeclaredPostScriptName);
			Entry->SetStringField(TEXT("declaredWeight"), Diagnostic.DeclaredWeight);
			Entry->SetStringField(TEXT("declaredStyle"), Diagnostic.DeclaredStyle);
			if (!Diagnostic.ResolvedAssetPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("resolvedAssetPath"), Diagnostic.ResolvedAssetPath);
			}
			if (!Diagnostic.FallbackAssetPath.IsEmpty())
			{
				Entry->SetStringField(TEXT("fallbackAssetPath"), Diagnostic.FallbackAssetPath);
			}
			if (!Diagnostic.FallbackToken.IsEmpty())
			{
				Entry->SetStringField(TEXT("fallbackToken"), Diagnostic.FallbackToken);
			}
			if (!Diagnostic.UnresolvedToken.IsEmpty())
			{
				Entry->SetStringField(TEXT("unresolvedToken"), Diagnostic.UnresolvedToken);
			}
			if (!Diagnostic.ResolvedTypeface.IsEmpty())
			{
				Entry->SetStringField(TEXT("resolvedTypeface"), Diagnostic.ResolvedTypeface);
			}
			Entry->SetStringField(TEXT("resolutionState"), Diagnostic.ResolutionState);
			Entry->SetStringField(TEXT("declarationSourceKind"), Diagnostic.DeclarationSourceKind);
			Entry->SetStringField(TEXT("resolutionSourceKind"), Diagnostic.ResolutionSourceKind);
			Entry->SetStringField(TEXT("fallbackState"), Diagnostic.FallbackState);
			Entry->SetStringField(TEXT("matchReason"), Diagnostic.MatchReason);
			Entry->SetStringField(TEXT("severity"), Diagnostic.Severity);

			TArray<TSharedPtr<FJsonValue>> IssueCodes;
			TArray<FString> SortedIssueCodes = Diagnostic.IssueCodes;
			SortedIssueCodes.Sort();
			for (const FString& IssueCode : SortedIssueCodes)
			{
				IssueCodes.Add(MakeShared<FJsonValueString>(IssueCode));
			}
			Entry->SetArrayField(TEXT("issueCodes"), IssueCodes);

			OutValues.Add(MakeShared<FJsonValueObject>(Entry));
		}

		void CollectFontResolutionResultsRecursive(
			const TSharedPtr<FDesignNode>& Node,
			const FString& NodePath,
			TArray<TSharedPtr<FJsonValue>>& OutValues,
			TSet<FString>& InOutSeenKeys)
		{
			if (!Node.IsValid())
			{
				return;
			}

			if (!Node->Text.FontFamily.IsEmpty() || !Node->Text.FontPostScriptName.IsEmpty())
			{
				FFontResolutionEntryKey EntryKey;
				EntryKey.NodeId = Node->Id;
				EntryKey.NodePath = NodePath;
				AddFontResolutionResult(
					OutValues,
					InOutSeenKeys,
					EntryKey,
					Node->Text.FontFamily,
					Node->Text.FontPostScriptName,
					Node->Text.FontWeight,
					Node->Text.FontStyle,
					false,
					false);
			}

			for (int32 RunIndex = 0; RunIndex < Node->Text.TextRuns.Num(); ++RunIndex)
			{
				const FDesignTextRun& Run = Node->Text.TextRuns[RunIndex];
				if (!Run.FontFamily.IsEmpty() || !Run.FontPostScriptName.IsEmpty())
				{
					FFontResolutionEntryKey EntryKey;
					EntryKey.EntryScope = TEXT("text-run");
					EntryKey.NodeId = Node->Id;
					EntryKey.NodePath = NodePath;
					EntryKey.RunId = RunIndex;
					AddFontResolutionResult(
						OutValues,
						InOutSeenKeys,
						EntryKey,
						Run.FontFamily,
						Run.FontPostScriptName,
						Run.FontWeight,
						Run.FontStyle,
						false,
						false);
				}
			}

			for (int32 ChildIndex = 0; ChildIndex < Node->Children.Num(); ++ChildIndex)
			{
				const FString ChildPath = FString::Printf(TEXT("%s/%d"), *NodePath, ChildIndex);
				CollectFontResolutionResultsRecursive(Node->Children[ChildIndex], ChildPath, OutValues, InOutSeenKeys);
			}
		}
	}

	bool Write(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FImportReportArtifacts& Artifacts,
		const TArray<FDesign2UMGMappingDiagnostic>* ComponentMappingDiagnostics,
		const TArray<FDesign2UMGMappingDiagnostic>* ReimportDiagnostics)
	{
		const FString ReportPath = BuildImportReportPath(Document);
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("schemaVersion"), TEXT("design2umg-import-report-v2"));
		Root->SetStringField(TEXT("reportPath"), ReportPath);
		Root->SetStringField(TEXT("pluginVersion"), ResolvePluginVersionName());
		Root->SetStringField(TEXT("documentVersion"), Document.Version);

		TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
		Source->SetStringField(TEXT("origin"), InferSourceOrigin(Document));
		if (!Document.Version.IsEmpty())
		{
			Source->SetStringField(TEXT("documentVersion"), Document.Version);
		}

		const FString SourceFilePath = ResolveDocumentSourceFilePath(Document);
		if (!SourceFilePath.IsEmpty())
		{
			Root->SetStringField(TEXT("sourceFilePath"), SourceFilePath);
			const FString SourceIdentity = DesignImageBakeUtils::BuildSourceDocumentIdentity(SourceFilePath);
			Root->SetStringField(TEXT("sourceIdentity"), SourceIdentity);
			Source->SetStringField(TEXT("filePath"), SourceFilePath);
			Source->SetStringField(TEXT("identity"), SourceIdentity);
		}

		TSharedRef<FJsonObject> Output = MakeShared<FJsonObject>();
		if (!Artifacts.RequestedOutputPath.IsEmpty())
		{
			Root->SetStringField(TEXT("requestedOutputPath"), Artifacts.RequestedOutputPath);
			Output->SetStringField(TEXT("requestedOutputPath"), Artifacts.RequestedOutputPath);
		}
		if (!Artifacts.RequestedRenderPngPath.IsEmpty())
		{
			Root->SetStringField(TEXT("requestedRenderPngPath"), Artifacts.RequestedRenderPngPath);
			Output->SetStringField(TEXT("requestedRenderPngPath"), Artifacts.RequestedRenderPngPath);
		}
		if (!Artifacts.GeneratedAssetPath.IsEmpty())
		{
			Root->SetStringField(TEXT("generatedAssetPath"), Artifacts.GeneratedAssetPath);
			Output->SetStringField(TEXT("generatedAssetPath"), Artifacts.GeneratedAssetPath);
		}
		if (!Artifacts.RenderPngPath.IsEmpty())
		{
			Root->SetStringField(TEXT("renderPngPath"), Artifacts.RenderPngPath);
			Output->SetStringField(TEXT("renderPngPath"), Artifacts.RenderPngPath);
		}
		if (Artifacts.Commit.IsSet())
		{
			const FDesign2UMGCommitResult& Commit = Artifacts.Commit.GetValue();
			TSharedRef<FJsonObject> Outcome = MakeShared<FJsonObject>();
			Outcome->SetStringField(TEXT("status"), CommitStatusName(Commit.Status));
			Outcome->SetBoolField(TEXT("succeeded"), Commit.IsSuccess());
			Outcome->SetStringField(TEXT("assetState"), FileStateName(Commit.Asset));
			Outcome->SetStringField(TEXT("manifestState"), FileStateName(Commit.Manifest));
			Outcome->SetStringField(TEXT("memoryState"), MemoryStateName(Commit.Memory));
			if (!Commit.AssetPath.IsEmpty()) Outcome->SetStringField(TEXT("assetPath"), Commit.AssetPath);
			Root->SetObjectField(TEXT("outcome"), Outcome);
		}

		TArray<FString> Recommendations;
		TArray<TSharedPtr<FJsonValue>> SkippedNodeValues;
		CollectSkippedDesignerNodes(Document, SkippedNodeValues, Recommendations);

		TArray<TSharedPtr<FJsonValue>> WarningValues;
		for (const FString& Warning : Data.Warnings)
		{
			WarningValues.Add(MakeShared<FJsonValueString>(Warning));
		}

		TArray<TSharedPtr<FJsonValue>> MissingFontValues;
		for (const FString& FontFamily : FFontResolver::FindMissingFonts(Document))
		{
			MissingFontValues.Add(MakeShared<FJsonValueString>(FontFamily));
		}
		if (MissingFontValues.Num() > 0)
		{
			AddUniqueRecommendation(
				Recommendations,
				TEXT("Map required fonts to project font assets or configure a default font; missing fonts should not be treated as an online-only runtime dependency."));
		}

		TArray<TSharedPtr<FJsonValue>> FontResolutionValues;
		TSet<FString> SeenFontResolutionKeys;
		for (int32 FontIndex = 0; FontIndex < Document.Fonts.Num(); ++FontIndex)
		{
			const FDesignFont& Font = Document.Fonts[FontIndex];
			FFontResolutionEntryKey EntryKey;
			EntryKey.EntryScope = TEXT("document-font-clue");
			EntryKey.DocumentFontIndex = FontIndex;
			AddFontResolutionResult(
				FontResolutionValues,
				SeenFontResolutionKeys,
				EntryKey,
				Font.Name,
				FString(),
				FString(),
				TEXT("normal"),
				IsUnsupportedFontFileSource(Font.Url),
				IsFontUrlSource(Font.Url));
		}
		for (int32 RootIndex = 0; RootIndex < Document.Children.Num(); ++RootIndex)
		{
			CollectFontResolutionResultsRecursive(
				Document.Children[RootIndex],
				LexToString(RootIndex),
				FontResolutionValues,
				SeenFontResolutionKeys);
		}
		TSharedRef<FJsonObject> FontResolution = MakeShared<FJsonObject>();
		FontResolution->SetArrayField(TEXT("results"), FontResolutionValues);

		TArray<TSharedPtr<FJsonValue>> DegradationValues;
		TSet<FString> SeenDegradationKeys;
		for (const TSharedPtr<FDesignNode>& RootNode : Document.Children)
		{
			CollectNodeDegradationsRecursive(RootNode, DegradationValues, SeenDegradationKeys, Recommendations);
		}

		TArray<TSharedPtr<FJsonValue>> EffectWrapperValues;
		CollectEffectWrappersRecursive(Data.RootWidget, EffectWrapperValues, Recommendations);

		TArray<TSharedPtr<FJsonValue>> ImageFailureValues;
		CollectImageFailureEntries(Artifacts.ImageReport, ImageFailureValues, Recommendations);

		TSharedRef<FJsonObject> Diagnostics = MakeShared<FJsonObject>();
		Diagnostics->SetArrayField(TEXT("warnings"), WarningValues);
		Diagnostics->SetArrayField(TEXT("skippedDesignerNodes"), SkippedNodeValues);
		Diagnostics->SetArrayField(TEXT("degradations"), DegradationValues);

		TSharedRef<FJsonObject> Resources = MakeShared<FJsonObject>();
		Resources->SetArrayField(TEXT("missingFonts"), MissingFontValues);
		Resources->SetArrayField(TEXT("imageFailures"), ImageFailureValues);
		if (!Artifacts.ImageReport.IsEmpty())
		{
			Resources->SetStringField(TEXT("imageImportSummary"), Artifacts.ImageReport);
		}
		if (const FString* PenExtensionResourceGap = Document.Metadata.Find(TEXT("design2umg.penExtensionResourceGap")))
		{
			Resources->SetStringField(TEXT("pencilExtensionResources"), *PenExtensionResourceGap);
			AddUniqueRecommendation(
				Recommendations,
				TEXT("Install the Pencil VS Code extension (or set the Pencil extension directory in the Design2UMG settings) so icon codepoints and library theme variables resolve from the extension resources."));
		}

		TSharedRef<FJsonObject> Effects = MakeShared<FJsonObject>();
		Effects->SetNumberField(TEXT("wrapperCount"), EffectWrapperValues.Num());
		Effects->SetArrayField(TEXT("wrappers"), EffectWrapperValues);

		TArray<TSharedPtr<FJsonValue>> RecommendationValues;
		for (const FString& Recommendation : Recommendations)
		{
			RecommendationValues.Add(MakeShared<FJsonValueString>(Recommendation));
		}

		// Component Mapping diagnostics: ini validation, hits, class-missing fallback, inputs overwrite/skip, Prototype inert.
		TSharedRef<FJsonObject> ComponentMapping = MakeShared<FJsonObject>();
		if (ComponentMappingDiagnostics && ComponentMappingDiagnostics->Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> MappingValues;
			for (const FDesign2UMGMappingDiagnostic& MappingDiagnostic : *ComponentMappingDiagnostics)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("kind"), MappingDiagnostic.Kind);
				Entry->SetStringField(TEXT("message"), MappingDiagnostic.Message);
				MappingValues.Add(MakeShared<FJsonValueObject>(Entry));
			}
			ComponentMapping->SetArrayField(TEXT("entries"), MappingValues);
		}

		// Safe Reimport per-node diagnostics (reimport_* entries); carried only by the reimport path - plain import reports have no such section.
		if (ReimportDiagnostics)
		{
			TSharedRef<FJsonObject> Reimport = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> ReimportValues;
			for (const FDesign2UMGMappingDiagnostic& ReimportDiagnostic : *ReimportDiagnostics)
			{
				TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
				Entry->SetStringField(TEXT("kind"), ReimportDiagnostic.Kind);
				Entry->SetStringField(TEXT("message"), ReimportDiagnostic.Message);
				ReimportValues.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Reimport->SetArrayField(TEXT("entries"), ReimportValues);
			Root->SetObjectField(TEXT("reimport"), Reimport);
		}

		Root->SetObjectField(TEXT("source"), Source);
		Root->SetObjectField(TEXT("output"), Output);
		Root->SetObjectField(TEXT("diagnostics"), Diagnostics);
		Root->SetObjectField(TEXT("resources"), Resources);
		Root->SetObjectField(TEXT("fontResolution"), FontResolution);
		Root->SetObjectField(TEXT("effects"), Effects);
		Root->SetObjectField(TEXT("componentMapping"), ComponentMapping);
		Root->SetArrayField(TEXT("recommendations"), RecommendationValues);

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
		FString Payload;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Payload);
		if (!FJsonSerializer::Serialize(Root, Writer)
			|| !FFileHelper::SaveStringToFile(Payload, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("DesignImportPipeline: could not write import diagnostic report %s"), *ReportPath);
			return false;
		}
		UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportPipeline: wrote import diagnostic report %s"), *ReportPath);
		return true;
	}

	bool WriteReimport(
		const FDesignDocument& Document,
		const FUMGWidgetData& Data,
		const FString& TargetAssetPath,
		const TArray<FDesign2UMGMappingDiagnostic>& ReimportDiagnostics,
		const TArray<FDesign2UMGMappingDiagnostic>* ComponentMappingDiagnostics,
		const FDesign2UMGCommitResult* Commit)
	{
		// requestedOutputPath carries the reimport target asset; its diagnostics go to the reimport section.
		FImportReportArtifacts Artifacts;
		Artifacts.RequestedOutputPath = TargetAssetPath;
		if (Commit) Artifacts.Commit = *Commit;
		return Write(Document, Data, Artifacts, ComponentMappingDiagnostics, &ReimportDiagnostics);
	}
}
