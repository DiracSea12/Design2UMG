#include "Automation/Design2UMGAutomationRender.h"

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
#include "RefExpander.h"
#include "UMGSchema.h"
#include "UMGWidgetUtils.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
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

	struct FStructureSnapshotStats
	{
		int32 TotalNodes = 0;
		int32 LeafNodes = 0;
		int32 ContainerNodes = 0;
		int32 TextNodes = 0;
		int32 VisualNodes = 0;
		int32 ButtonNodes = 0;
		int32 PlaceholderNodes = 0;
		int32 MaxDepth = 0;
		int32 RootChildCount = 0;
		FString RootKind = TEXT("unknown");
		TArray<FString> VisibleTexts;
		TArray<FString> BorderWidgets;
		TMap<FString, int32> KindHistogram;
		TMap<FString, int32> ClassHistogram;

		void AddKind(const FString& Kind)
		{
			KindHistogram.FindOrAdd(Kind)++;
		}

		void AddClass(const UWidget* Widget)
		{
			if (!Widget)
			{
				return;
			}
			ClassHistogram.FindOrAdd(Widget->GetClass()->GetName())++;
		}

		TSharedPtr<FJsonObject> ToJson(const FString& SnapshotType) const
		{
			TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetStringField(TEXT("snapshot_type"), SnapshotType);
			Root->SetStringField(TEXT("root_kind"), RootKind);
			Root->SetNumberField(TEXT("root_child_count"), RootChildCount);
			Root->SetNumberField(TEXT("total_nodes"), TotalNodes);
			Root->SetNumberField(TEXT("leaf_nodes"), LeafNodes);
			Root->SetNumberField(TEXT("container_nodes"), ContainerNodes);
			Root->SetNumberField(TEXT("text_nodes"), TextNodes);
			Root->SetNumberField(TEXT("visual_nodes"), VisualNodes);
			Root->SetNumberField(TEXT("button_nodes"), ButtonNodes);
			Root->SetNumberField(TEXT("placeholder_nodes"), PlaceholderNodes);
			Root->SetNumberField(TEXT("max_depth"), MaxDepth);

			TArray<FString> SortedTexts = VisibleTexts;
			SortedTexts.Sort();
			TArray<TSharedPtr<FJsonValue>> TextValues;
			for (const FString& Text : SortedTexts)
			{
				TextValues.Add(MakeShared<FJsonValueString>(Text));
			}
			Root->SetArrayField(TEXT("visible_texts"), TextValues);

			TArray<FString> SortedBorders = BorderWidgets;
			SortedBorders.Sort();
			TArray<TSharedPtr<FJsonValue>> BorderValues;
			for (const FString& BorderName : SortedBorders)
			{
				BorderValues.Add(MakeShared<FJsonValueString>(BorderName));
			}
			Root->SetArrayField(TEXT("border_widgets"), BorderValues);

			TSharedPtr<FJsonObject> HistogramObject = MakeShared<FJsonObject>();
			for (const TPair<FString, int32>& Pair : KindHistogram)
			{
				HistogramObject->SetNumberField(Pair.Key, Pair.Value);
			}
			Root->SetObjectField(TEXT("kind_histogram"), HistogramObject);

			TSharedPtr<FJsonObject> ClassHistogramObject = MakeShared<FJsonObject>();
			for (const TPair<FString, int32>& Pair : ClassHistogram)
			{
				ClassHistogramObject->SetNumberField(Pair.Key, Pair.Value);
			}
			Root->SetObjectField(TEXT("class_histogram"), ClassHistogramObject);
			return Root;
		}
	};

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

	FString NormalizeStructureText(const FString& InText)
	{
		FString Normalized = InText;
		Normalized.ReplaceInline(TEXT("\r"), TEXT(" "));
		Normalized.ReplaceInline(TEXT("\n"), TEXT(" "));
		Normalized.ReplaceInline(TEXT("\t"), TEXT(" "));

		TArray<FString> Parts;
		Normalized.ParseIntoArrayWS(Parts);
		return FString::Join(Parts, TEXT(" "));
	}

	FString NormalizeCompactStructureKey(const FString& InValue)
	{
		FString Key = InValue.ToLower().TrimStartAndEnd();
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		return Key;
	}

	bool IsIconLikeFontFamily(const FString& FontFamily)
	{
		const FString NormalizedFamily = NormalizeCompactStructureKey(FontFamily);
		return NormalizedFamily.Contains(TEXT("lucide"))
			|| NormalizedFamily.Contains(TEXT("materialsymbol"))
			|| NormalizedFamily.Contains(TEXT("materialicon"));
	}

	bool ShouldSkipDesignNodeForStructure(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->IsCollapsed())
		{
			return true;
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

	TSharedPtr<FDesignNode> ResolveEffectiveDesignNodeForStructure(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc)
	{
		if (!Node.IsValid())
		{
			return nullptr;
		}

		if (Node->Type == EDesignNodeType::Ref)
		{
			if (TSharedPtr<FDesignNode> Expanded = FRefExpander::ExpandRef(Node, Doc))
			{
				return Expanded;
			}
		}

		return Node;
	}

	FString ClassifyDesignNodeKindForStructure(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return TEXT("unknown");
		}
		if (Node->LayoutData.bSemanticPlaceholderSurface)
		{
			return TEXT("placeholder");
		}

		switch (Node->Type)
		{
		case EDesignNodeType::Frame:
		case EDesignNodeType::Group:
			return TEXT("container");
		case EDesignNodeType::Text:
		case EDesignNodeType::IconFont:
			return TEXT("text");
		case EDesignNodeType::Rectangle:
		case EDesignNodeType::Ellipse:
		case EDesignNodeType::Line:
		case EDesignNodeType::Polygon:
		case EDesignNodeType::Path:
			return TEXT("visual");
		default:
			return TEXT("other");
		}
	}

	void GatherDesignStructureChildren(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignDocument& Doc,
		TArray<TSharedPtr<FDesignNode>>& OutChildren)
	{
		OutChildren.Reset();
		if (!Node.IsValid())
		{
			return;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			TSharedPtr<FDesignNode> EffectiveChild = ResolveEffectiveDesignNodeForStructure(Child, Doc);
			if (!ShouldSkipDesignNodeForStructure(EffectiveChild))
			{
				OutChildren.Add(EffectiveChild);
			}
		}
	}

	void AccumulateDesignStructureSnapshot(
		const TSharedPtr<FDesignNode>& Node,
		const FDesignDocument& Doc,
		const int32 Depth,
		FStructureSnapshotStats& Stats)
	{
		TSharedPtr<FDesignNode> EffectiveNode = ResolveEffectiveDesignNodeForStructure(Node, Doc);
		if (ShouldSkipDesignNodeForStructure(EffectiveNode))
		{
			return;
		}

		Stats.TotalNodes++;
		Stats.MaxDepth = FMath::Max(Stats.MaxDepth, Depth);

		const FString Kind = ClassifyDesignNodeKindForStructure(EffectiveNode);
		Stats.AddKind(Kind);
		if (Kind == TEXT("container"))
		{
			Stats.ContainerNodes++;
		}
		else if (Kind == TEXT("text"))
		{
			Stats.TextNodes++;
			const FString Text = (EffectiveNode->Type == EDesignNodeType::IconFont
				|| IsIconLikeFontFamily(EffectiveNode->Text.FontFamily)
				|| IsIconLikeFontFamily(EffectiveNode->Text.IconFontFamily))
				? FString()
				: NormalizeStructureText(EffectiveNode->Text.Content);
			if (!Text.IsEmpty())
			{
				Stats.VisibleTexts.Add(Text);
			}
		}
		else if (Kind == TEXT("visual"))
		{
			Stats.VisualNodes++;
		}
		else if (Kind == TEXT("placeholder"))
		{
			Stats.PlaceholderNodes++;
			Stats.VisualNodes++;
		}

		TArray<TSharedPtr<FDesignNode>> Children;
		GatherDesignStructureChildren(EffectiveNode, Doc, Children);
		if (Depth == 0)
		{
			Stats.RootChildCount = Children.Num();
		}
		if (Children.Num() == 0)
		{
			Stats.LeafNodes++;
			return;
		}

		for (const TSharedPtr<FDesignNode>& Child : Children)
		{
			AccumulateDesignStructureSnapshot(Child, Doc, Depth + 1, Stats);
		}
	}

	TSharedPtr<FDesignNode> FindDesignNodeByIdRecursive(const TSharedPtr<FDesignNode>& Node, const FString& TargetId)
	{
		if (!Node.IsValid())
		{
			return nullptr;
		}
		if (Node->Id == TargetId)
		{
			return Node;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (TSharedPtr<FDesignNode> Match = FindDesignNodeByIdRecursive(Child, TargetId))
			{
				return Match;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> BuildDesignStructureSnapshot(const FDesignDocument& Doc, const FString& PreferredRootId = FString())
	{
		FStructureSnapshotStats Stats;

		if (!PreferredRootId.IsEmpty())
		{
			for (const TSharedPtr<FDesignNode>& Child : Doc.Children)
			{
				if (TSharedPtr<FDesignNode> Match = FindDesignNodeByIdRecursive(Child, PreferredRootId))
				{
					TSharedPtr<FDesignNode> RootNode = ResolveEffectiveDesignNodeForStructure(Match, Doc);
					if (ShouldSkipDesignNodeForStructure(RootNode))
					{
						return nullptr;
					}

					Stats.RootKind = ClassifyDesignNodeKindForStructure(RootNode);
					AccumulateDesignStructureSnapshot(RootNode, Doc, 0, Stats);
					return Stats.ToJson(TEXT("design_semantic"));
				}
			}
		}

		if (Doc.Children.Num() == 1)
		{
			TSharedPtr<FDesignNode> RootNode = ResolveEffectiveDesignNodeForStructure(Doc.Children[0], Doc);
			if (ShouldSkipDesignNodeForStructure(RootNode))
			{
				return nullptr;
			}

			Stats.RootKind = ClassifyDesignNodeKindForStructure(RootNode);
			AccumulateDesignStructureSnapshot(RootNode, Doc, 0, Stats);
			return Stats.ToJson(TEXT("design_semantic"));
		}

		Stats.RootKind = TEXT("document");
		for (const TSharedPtr<FDesignNode>& Child : Doc.Children)
		{
			TSharedPtr<FDesignNode> EffectiveChild = ResolveEffectiveDesignNodeForStructure(Child, Doc);
			if (ShouldSkipDesignNodeForStructure(EffectiveChild))
			{
				continue;
			}

			Stats.RootChildCount++;
			AccumulateDesignStructureSnapshot(EffectiveChild, Doc, 1, Stats);
		}
		return Stats.ToJson(TEXT("design_semantic"));
	}

	bool IsWidgetVisibleForStructure(const UWidget* Widget)
	{
		if (!Widget)
		{
			return false;
		}

		const ESlateVisibility Visibility = Widget->GetVisibility();
		return Visibility != ESlateVisibility::Collapsed && Visibility != ESlateVisibility::Hidden;
	}

	void GatherWidgetStructureChildren(UWidget* Widget, TArray<UWidget*>& OutChildren)
	{
		OutChildren.Reset();
		if (!Widget)
		{
			return;
		}

		TSet<UWidget*> Seen;
		auto TryAddChild = [&OutChildren, &Seen](UWidget* Child)
		{
			if (!IsWidgetVisibleForStructure(Child) || Seen.Contains(Child))
			{
				return;
			}
			Seen.Add(Child);
			OutChildren.Add(Child);
		};

		if (UUserWidget* UserWidget = Cast<UUserWidget>(Widget))
		{
			if (UserWidget->WidgetTree && UserWidget->WidgetTree->RootWidget)
			{
				TryAddChild(UserWidget->WidgetTree->RootWidget);
			}
		}

		if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
		{
			for (int32 ChildIndex = 0; ChildIndex < PanelWidget->GetChildrenCount(); ++ChildIndex)
			{
				TryAddChild(PanelWidget->GetChildAt(ChildIndex));
			}
			return;
		}

		if (UContentWidget* ContentWidget = Cast<UContentWidget>(Widget))
		{
			TryAddChild(ContentWidget->GetContent());
		}
	}

	FString ClassifyWidgetKindForStructure(UWidget* Widget)
	{
		if (!Widget)
		{
			return TEXT("unknown");
		}
		if (Cast<UTextBlock>(Widget))
		{
			return TEXT("text");
		}
		if (Cast<UButton>(Widget))
		{
			return TEXT("button");
		}
		if (Cast<UImage>(Widget))
		{
			return TEXT("visual");
		}
		if (Cast<USpacer>(Widget))
		{
			return TEXT("spacer");
		}
		if (UBorder* Border = Cast<UBorder>(Widget))
		{
			TArray<UWidget*> Children;
			GatherWidgetStructureChildren(Border, Children);
			return Children.Num() > 0 ? TEXT("container") : TEXT("visual");
		}
		if (Cast<UPanelWidget>(Widget) || Cast<UContentWidget>(Widget))
		{
			return TEXT("container");
		}
		return TEXT("other");
	}

	FString ResolveWidgetStructureText(const UTextBlock* TextBlock)
	{
		if (!TextBlock)
		{
			return FString();
		}

		const FString RawText = NormalizeStructureText(TextBlock->GetText().ToString());
		if (RawText.IsEmpty())
		{
			return FString();
		}

		const FSlateFontInfo& FontInfo = TextBlock->GetFont();
		if (FontInfo.FontObject)
		{
			const FString FontObjectName = FontInfo.FontObject->GetName();
			const FString FontObjectPath = FontInfo.FontObject->GetPathName();
			if (IsIconLikeFontFamily(FontObjectName) || IsIconLikeFontFamily(FontObjectPath))
			{
				return FString();
			}

			const FString SemanticIconName = FFontResolver::FindKnownIconNameByGlyph(
				FontObjectName,
				RawText);
			if (!SemanticIconName.IsEmpty())
			{
				return FString();
			}
		}

		return RawText;
	}

	void AccumulateWidgetStructureSnapshot(UWidget* Widget, const int32 Depth, FStructureSnapshotStats& Stats)
	{
		if (!IsWidgetVisibleForStructure(Widget))
		{
			return;
		}

		Stats.TotalNodes++;
		Stats.MaxDepth = FMath::Max(Stats.MaxDepth, Depth);
		Stats.AddClass(Widget);
		if (Cast<UBorder>(Widget))
		{
			Stats.BorderWidgets.Add(Widget->GetName());
		}

		const FString Kind = ClassifyWidgetKindForStructure(Widget);
		Stats.AddKind(Kind);
		if (Kind == TEXT("container"))
		{
			Stats.ContainerNodes++;
		}
		else if (Kind == TEXT("text"))
		{
			Stats.TextNodes++;
			if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				const FString Text = ResolveWidgetStructureText(TextBlock);
				if (!Text.IsEmpty())
				{
					Stats.VisibleTexts.Add(Text);
				}
			}
		}
		else if (Kind == TEXT("visual"))
		{
			Stats.VisualNodes++;
		}
		else if (Kind == TEXT("button"))
		{
			Stats.ButtonNodes++;
		}

		TArray<UWidget*> Children;
		GatherWidgetStructureChildren(Widget, Children);
		if (Depth == 0)
		{
			Stats.RootChildCount = Children.Num();
		}
		if (Children.Num() == 0)
		{
			Stats.LeafNodes++;
			return;
		}

		for (UWidget* Child : Children)
		{
			AccumulateWidgetStructureSnapshot(Child, Depth + 1, Stats);
		}
	}

	TSharedPtr<FJsonObject> BuildWidgetTreeStructureSnapshot(UWidgetTree* WidgetTree)
	{
		if (!WidgetTree || !WidgetTree->RootWidget)
		{
			return nullptr;
		}

		FStructureSnapshotStats Stats;
		Stats.RootKind = ClassifyWidgetKindForStructure(WidgetTree->RootWidget);
		AccumulateWidgetStructureSnapshot(WidgetTree->RootWidget, 0, Stats);
		return Stats.ToJson(TEXT("widget_tree_semantic"));
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
		FUMGWidgetData& OutData,
		FString& OutError)
	{
		if (!Design2UMGLocalFigmaImport::LoadLocalFigmaDocument(SourcePath, OutDocument, OutError, TargetNodeId))
		{
			return false;
		}

		DesignFontImportService::PrepareDocumentFonts(OutDocument);
		DesignImportPipeline::ConvertDocument(OutDocument, OutData);
		DesignImportPipeline::WriteImportDiagnosticReport(OutDocument, OutData);
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
			Request.Generation.OutGeneratedAssetPath = &OutResult.GeneratedAssetPath;
			Request.Generation.OutRenderedSize = &OutResult.RenderSize;
			return Request;
		};

		auto CaptureGeneratedAssetStructure = [&]()
		{
			if (!OutResult.GeneratedAssetPath.IsEmpty())
			{
				if (UWidgetBlueprint* GeneratedBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *OutResult.GeneratedAssetPath))
				{
					OutResult.AssetStructure = BuildWidgetTreeStructureSnapshot(GeneratedBlueprint->WidgetTree);
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
			OutResult.AssetStructure = BuildWidgetTreeStructureSnapshot(WidgetBlueprint->WidgetTree);
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

			OutResult.SourceStructure = BuildDesignStructureSnapshot(Document, Spec.NodeId);

			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: generating blueprint and compiled png"));
			if (!DesignImportPipeline::Execute(Document, MakeAutomationImportRequest(true)).bSucceeded)
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
		FString DeferredImageImportReport;
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

			OutResult.SourceStructure = BuildDesignStructureSnapshot(SourceDocument, Spec.NodeId);

			FString ImageReport;
			DesignImportPipeline::ResolveImages(Data, &ImageReport);
			DeferredImageImportReport = MoveTemp(ImageReport);
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

			OutResult.SourceStructure = BuildDesignStructureSnapshot(SourceDocument, Spec.NodeId);
			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: generating blueprint and compiled png"));
			if (!DesignImportPipeline::Execute(SourceDocument, MakeAutomationImportRequest(true)).bSucceeded)
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
			if (!LoadFigmaSourceData(Spec.SourcePath, Spec.NodeId, SourceDocument, Data, ErrorMessage))
			{
				OutResult.ErrorMessage = ErrorMessage;
				return false;
			}

			OutResult.SourceStructure = BuildDesignStructureSnapshot(SourceDocument, Spec.NodeId);
			FString ImageReport;
			DesignImportPipeline::ResolveImages(Data, &ImageReport);
			DeferredImageImportReport = MoveTemp(ImageReport);
		}
		else
		{
			OutResult.ErrorMessage = FString::Printf(TEXT("Unsupported automation render mode: %s"), *Spec.Mode);
			return false;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: generating blueprint and compiled png"));
		if (!DesignImportPipeline::Execute(Data, MakeAutomationImportRequest(false)).bSucceeded)
		{
			OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
			return false;
		}

		if (!SourceDocument.Children.IsEmpty())
		{
			DesignImportPipeline::WriteImportDiagnosticReport(
				SourceDocument,
				Data,
				DeferredImageImportReport,
				Spec.OutputPath,
				Spec.RenderPngPath,
				OutResult.GeneratedAssetPath);
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

