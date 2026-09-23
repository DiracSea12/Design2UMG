// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGComponentBlueprints.h"
#include "DesignImportReimportInternal.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Algo/AnyOf.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "Design2UMGComponentMappingGeneration.h"
#include "Design2UMGGenerationManifest.h"
#include "Design2UMGLog.h"
#include "DesignImageResourceLimits.h"
#include "Design2UMGLocalFigmaImport.h"
#include "FigmaComponentDependencies.h"
#include "FigmaParser.h"
#include "DesignLocalFontImport.h"
#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "UMGJsonSerializer.h"
#include "UMGSchemaSourceExtensions.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

namespace
{
	FString StableName(const FString& Scope, const FString& Identity)
	{
		const FTCHARToUTF8 Bytes(*(Scope + TEXT("|") + Identity));
		return TEXT("WBP_") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length());
	}

	FName SlotName(const FString& Path)
	{
		return FUMGWidgetUtils::SanitizeWidgetName(TEXT("D2U_Content_") + Path, TEXT("NamedSlot"));
	}

	void ExposeContent(FUMGWidgetNode& Node, const FString& Path,
		const FDesign2UMGComponentBlueprintContext& Context)
	{
		FUMGWidgetNode Content = MoveTemp(Node);
		if (Context.ResolveKey(Content).IsEmpty())
		{
			for (int32 Index = 0; Index < Content.Children.Num(); ++Index)
			{
				ExposeContent(Content.Children[Index], Path + TEXT("_") + FString::FromInt(Index), Context);
			}
		}
		Node = FUMGWidgetNode();
		Node.Type = TEXT("NamedSlot");
		Node.Name = SlotName(Path).ToString();
		Node.Id = Node.Name;
		Node.MutableSource().SourceNodeId = Content.GetStableDesignNodeId();
		Node.Bounds = Content.Bounds;
		Node.Slot = Content.Slot;
		Content.Slot = FUMGSlotData();
		Node.Children.Add(MoveTemp(Content));
	}

	FString LocalAppearance(const FUMGWidgetNode& Node, const bool Root)
	{
		FUMGWidgetNode Local;
		Local.Type = Node.Type;
		Local.Bounds = Node.Bounds;
		Local.Slot = Node.Slot;
		Local.TextStyle = Node.TextStyle;
		Local.WidgetStyle = Node.WidgetStyle;
		Local.Text = Node.Text;
		Local.Brush = Node.Brush;
		Local.Effect = Node.Effect;
		Local.Path = Node.Path;
		Local.Shadow = Node.Shadow;
		Local.Render = Node.Render;
		Local.WidgetState = Node.WidgetState;
		Local.ScrollBox = Node.ScrollBox;
		// Child placement belongs to this container. Replace the container if its layout changes.
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			FUMGWidgetNode Placement;
			Placement.Bounds = Child.Bounds;
			Placement.Slot = Child.Slot;
			Local.Children.Add(MoveTemp(Placement));
		}
		if (Root)
		{
			Local.Bounds.X = Local.Bounds.Y = 0;
			Local.Slot = FUMGSlotData();
		}
		FString Json;
		FJsonSerializer::Serialize(FUMGJsonSerializer::SerializeWidgetNode(Local), TJsonWriterFactory<>::Create(&Json));
		return Json;
	}

	using FComponentFolders = TMap<FName, TArray<FString>>;

	FComponentFolders IndexComponentFolders(const FString& ComponentsRoot)
	{
		FComponentFolders Folders;
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get()
			.GetAssetsByPath(FName(*ComponentsRoot), Assets, true);
		for (const FAssetData& Asset : Assets)
			Folders.FindOrAdd(Asset.AssetName).AddUnique(Asset.PackagePath.ToString());
		return Folders;
	}

	bool ResolveExistingComponentFolder(const FComponentFolders& Folders, const FString& AssetName,
		FString& InOutFolder, FString& Error)
	{
		const TArray<FString>* Matches = Folders.Find(FName(*AssetName));
		if (!Matches) return true;
		if (Matches->Num() != 1)
		{
			Error = FString::Printf(TEXT("Multiple generated asset paths for component '%s': '%s' and '%s'"),
				*AssetName, *((*Matches)[0] / AssetName), *((*Matches)[1] / AssetName));
			return false;
		}
		InOutFolder = (*Matches)[0];
		return true;
	}

	// GetSlotNames reads the current generated class. Validate before any dependency
	// compilation removes old slots, including content of instances removed by the design.
	bool ValidateExistingInstanceContent(UWidgetBlueprint* Blueprint,
		const FDesign2UMGGenerationManifestSidecar& Manifest, FString& Error)
	{
		const TSet<FString> Owned(Manifest.D2UOwned);
		TSet<FString> MappedInstances;
		for (const auto& Record : Manifest.Instances)
		{
			if (Record.Kind == TEXT("mapped")) MappedInstances.Add(Record.WidgetName);
		}
		Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			UUserWidget* Instance = Cast<UUserWidget>(Widget);
			if (!Instance || !Owned.Contains(Instance->GetName())
				|| MappedInstances.Contains(Instance->GetName()) || !Error.IsEmpty()) return;
			TArray<FName> Slots;
			Instance->GetSlotNames(Slots);
			for (const FName Slot : Slots)
			{
				UWidget* Content = Instance->GetContentForSlot(Slot);
				FString Reason;
				if (Content && !DesignImportPipeline::IsSubtreePurelyD2UOwned(Blueprint, Content, Owned, Reason))
				{
					Error = FString::Printf(TEXT("Instance '%s' slot '%s' contains user changes: %s"),
						*Instance->GetName(), *Slot.ToString(), *Reason);
					break;
				}
			}
		});
		return Error.IsEmpty();
	}


	bool FindExisting(const FUMGWidgetData& Data, const FString& Folder,
		UWidgetBlueprint*& Blueprint, FString& Error, bool bWholeAssetOwned = true)
	{
		const FString Package = Folder / Data.WidgetName;
		UObject* Asset = LoadObject<UObject>(nullptr, *(Package + TEXT(".") + Data.WidgetName), nullptr, LOAD_NoWarn);
		Blueprint = Cast<UWidgetBlueprint>(Asset);
		if (!Blueprint)
		{
			if (Asset || FPackageName::DoesPackageExist(Package))
			{
				Error = FString::Printf(TEXT("Generated asset path '%s' is occupied or failed to load as a Widget Blueprint"), *Package);
				return false;
			}
			return true;
		}
		FDesign2UMGGenerationManifestSidecar Sidecar;
		if (Blueprint->GetOutermost()->IsDirty()
			|| !Design2UMGGenerationManifest::TryLoadSidecarForAsset(Package, Sidecar, Error)
			|| (bWholeAssetOwned && (Sidecar.GeneratedAssetHash.IsEmpty()
				|| Sidecar.GeneratedAssetHash != Design2UMGGenerationManifest::ComputeSavedAssetHash(Package)))
			|| Sidecar.DocumentScope != Data.SourceDocument().DocumentScope
			|| Sidecar.SourceRootNodeId != Data.RootWidget.GetStableDesignNodeId())
		{
			Error = FString::Printf(TEXT("Generated asset conflict at '%s': user changes or missing ownership record. %s"), *Package, *Error);
			return false;
		}
		return true;
	}
}

FString FDesign2UMGComponentBlueprintContext::ResolveKey(const FUMGWidgetNode& Node) const
{
	if (!Node.HasSourceData() || Node.Source().SourceNodeType != TEXT("INSTANCE")) return FString();
	if (!Node.Source().MainComponentKey.IsEmpty()) return Node.Source().MainComponentKey;
	if (const FString* Key = NodeIdToKey.Find(Node.Source().ComponentId)) return *Key;
	return Node.Source().ComponentId;
}

bool FDesign2UMGComponentBlueprintContext::ApplyInstance(UUserWidget* Instance, UWidgetTree* OwnerTree,
	const FUMGWidgetNode& Node, const FDesign2UMGGenerationSettingsSnapshot& Settings,
	const TMap<FString, UTexture2D*>* Textures, const FIntPoint* DesignSize, FDesign2UMGComponentMappingBuildState* Ownership)
{
	const FComponent* Component = Components.Find(ResolveKey(Node));
	if (!Component || !Instance) return false;
	TFunction<bool(const FUMGWidgetNode&, const FUMGWidgetNode&, const FString&)> Apply;
	Apply = [&](const FUMGWidgetNode& Default, const FUMGWidgetNode& Actual, const FString& Path)
	{
		if (!ResolveKey(Default).IsEmpty()
			|| Default.Children.Num() != Actual.Children.Num()
			|| LocalAppearance(Default, Path == TEXT("0")) != LocalAppearance(Actual, Path == TEXT("0")))
		{
			FUMGWidgetNode Content = Actual;
			// The surrounding parent owns the instance position, not its replacement content.
			if (Path == TEXT("0"))
			{
				Content.MutableSource().ComponentId.Reset();
				Content.MutableSource().MainComponentKey.Reset();
				Content.MutableSource().SourceNodeType = TEXT("FRAME");
				Content.MutableSource().ComponentPropertiesJson.Reset();
				Content.MutableSource().VariantPropertiesJson.Reset();
				Content.MutableSource().OverridesJson.Reset();
			}
			Content.Slot = FUMGSlotData();
			FDesign2UMGComponentMappingBuildState Mapping(Settings.ComponentMapping);
			Design2UMGComponentMappingGeneration::ApplyMappingToWidgetTreeIR(Content, Settings.ComponentMapping, Component->Data, Mapping);
			UWidget* Widget = FUMGWidgetUtils::CreateWidgetRecursive(OwnerTree, Content, nullptr, Textures, DesignSize, &Mapping, nullptr, &Settings);
			if (!Widget) return false;
			if (Ownership) Ownership->IncludeInstanceContent(Mapping);
			Instance->SetContentForSlot(SlotName(Path), Widget);
			if (Instance->GetContentForSlot(SlotName(Path)) != Widget)
			{
				Error = FString::Printf(TEXT("Component '%s': instance slot '%s' did not retain its override"), *ResolveKey(Node), *SlotName(Path).ToString());
				UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Error);
				return false;
			}
			return true;
		}
		for (int32 Index = 0; Index < Default.Children.Num(); ++Index)
		{
			if (!Apply(Default.Children[Index], Actual.Children[Index], Path + TEXT("_") + FString::FromInt(Index))) return false;
		}
		return true;
	};
	return Apply(Component->Data.RootWidget, Node, TEXT("0"));
}

UWidgetBlueprint* FDesign2UMGComponentBlueprintContext::Build(const FUMGWidgetData& Data,
	const FString& Folder, const FDesign2UMGGenerationSettingsSnapshot* Settings,
	FDesign2UMGGenerationOutput* Output, FUMGPrototypeNavigationData* Navigation, UWidgetBlueprint* TargetBlueprint)
{
	// Some reimport entry points call the component batch directly. Own one budget there;
	// calls reached through FUMGGenerator already have one and this scope simply reuses it.
	DesignImageResources::FScopedGeneratedTextureOperation GeneratedTextureOperation;

	FDesign2UMGGenerationSettingsSnapshot Effective = Settings ? *Settings
		: FDesign2UMGGenerationSettingsSnapshot::FromSettings(*UDesign2UMGSettings::Get());
	Effective.ComponentBlueprints = MakeShared<FDesign2UMGComponentBlueprintContext>();
	auto& Context = *Effective.ComponentBlueprints;
	Context.DocumentScope = Data.SourceDocument().DocumentScope;
	TArray<UWidgetBlueprint*> TouchedAssets;
	int32 BulkTransactionIndex = INDEX_NONE;
	auto EndBulkTransaction = [&](FDesign2UMGGenerationOutput& Result)
	{
		if (BulkTransactionIndex == INDEX_NONE) return;
		// Capture our identity before End; an empty transaction can be removed there.
		Result.ComponentBulkTransactionId = GUndo ? GUndo->GetContext().TransactionId : FGuid();
		GEditor->EndTransaction();
		BulkTransactionIndex = INDEX_NONE;
	};
	auto Fail = [&](const TCHAR* Kind = TEXT("component_generation_failed"),
		EDesign2UMGCommitStatus FailureStatus = EDesign2UMGCommitStatus::NotCommitted) -> UWidgetBlueprint*
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Component import failed: %s"), *Context.Error);
		if (Output) Output->MappingDiagnostics.Add({Kind, Context.Error});
		if (Output) EndBulkTransaction(*Output);
		const EDesign2UMGMemoryState Memory = FUMGGenerator::RestoreUnsavedComponentBulk(TouchedAssets, Output);
		if (Output)
		{
			Output->Commit.Memory = Memory;
			Output->Commit.Status = Memory == EDesign2UMGMemoryState::RecoveryFailed
				? EDesign2UMGCommitStatus::RollbackIncomplete : FailureStatus;
			if (TargetBlueprint) Output->Commit.AssetPath = TargetBlueprint->GetPathName();
		}
		return nullptr;
	};
	FigmaComponentDependencies::FCatalog Catalog;
	if (Context.DocumentScope.IsEmpty() || !Output)
	{
		Context.Error = TEXT("Component batch requires a stable document identity and generation output for dependency saves");
		return Fail();
	}
	if (!FigmaComponentDependencies::Parse(Data.SourceDocument().ComponentDependenciesJson, Catalog, Context.Error)) return Fail();
	TSet<FString> MappedKeys;
	for (const auto& Entry : Effective.ComponentMapping.Table.Entries)
	{
		if (Entry.Provider == TEXT("figma") && Entry.DocumentScope == Context.DocumentScope)
		{
			MappedKeys.Add(Entry.ComponentKey);
		}
	}
	for (const auto& Pair : Catalog.Definitions)
	{
		Context.NodeIdToKey.Add(Pair.Value.NodeId, Pair.Key);
		DesignComponentMapping::FDesignComponentIdentity Identity;
		Identity.Provider = TEXT("figma");
		Identity.DocumentScope = Context.DocumentScope;
		Identity.ComponentKey = Pair.Key;
		// Published set identity is shared by user mapping and dependency traversal.
		Identity.ComponentSetKey = Pair.Value.ComponentSetKey;
		bool AtSet = false;
		if (Effective.ComponentMapping.Table.Match(Identity, AtSet)) MappedKeys.Add(Pair.Key);
	}
	TArray<FString> Requested;
	TFunction<void(const FUMGWidgetNode&)> Collect = [&](const FUMGWidgetNode& Node)
	{
		const FString Key = Context.ResolveKey(Node);
		if (!Key.IsEmpty())
		{
			DesignComponentMapping::FDesignComponentIdentity Identity;
			bool AtSet = false;
			if (DesignComponentMapping::TryResolveFigmaComponentIdentity(Node.Source(), Context.DocumentScope, Identity)
				&& Effective.ComponentMapping.Table.Match(Identity, AtSet)) MappedKeys.Add(Key);
			Requested.AddUnique(Key);
			if (MappedKeys.Contains(Key)) return;
		}
		for (const auto& Child : Node.Children) Collect(Child);
	};
	Collect(Data.RootWidget);
	TArray<FString> Order;
	if (!FigmaComponentDependencies::BuildOrder(Catalog, Requested, MappedKeys, Order, Context.Error)) return Fail();
	DesignLocalFontImport::FSourceIndex FontSources;
	TMap<FString, UWidgetBlueprint*> Existing;
	const FComponentFolders ComponentFolders = IndexComponentFolders(Folder / TEXT("Components"));
	for (const FString& Key : Order)
	{
		const auto& Definition = Catalog.Definitions[Key];
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("fileKey"), Context.DocumentScope);
		Json->SetObjectField(TEXT("document"), Definition.Document);
		FString Serialized;
		FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Serialized));
		FDesignDocument Document;
		if (!FFigmaParser::ParseDocument(Serialized, Document))
		{
			Context.Error = TEXT("Failed to parse component definition: ") + Key;
			return Fail();
		}
		FComponent& Component = Context.Components.Add(Key);
		TSet<FString> ImageRefs;
		FFigmaParser::CollectImageRefs(Document, ImageRefs);
		for (const FString& Ref : ImageRefs)
		{
			const FString* Url = Data.SourceDocument().ResolvedComponentImageUrls.Find(Ref);
			const bool HasLocalImage = !Data.SourceDocument().SourceFilePath.IsEmpty()
				&& FPaths::FileExists(FPaths::GetPath(Data.SourceDocument().SourceFilePath) / TEXT("images") / (Ref + TEXT(".png")));
			if ((!Url || Url->IsEmpty()) && !HasLocalImage)
			{
				Context.Error = FString::Printf(TEXT("Component '%s' is missing image '%s'"), *Key, *Ref);
				return Fail();
			}
		}
		FFigmaParser::ApplyImageUrls(Document, Data.SourceDocument().ResolvedComponentImageUrls);
		if (!Data.SourceDocument().SourceFilePath.IsEmpty())
		{
			Design2UMGLocalFigmaImport::ResolveDocumentImages(Document, Data.SourceDocument().SourceFilePath);
		}
		// A selected definition is the asset body, rather than an off-page reusable library entry.
		for (const auto& RootNode : Document.Children)
		{
			RootNode->bReusable = false;
			// Library placement visibility is not the visibility of a component instance.
			// Each instance keeps its own visibility in the parent IR.
			RootNode->Transform.bEnabled = true;
			// An empty component is still a reusable, sized template. With no children,
			// hug-content estimation has nothing to measure; keep its declared extent.
			if (RootNode->Children.IsEmpty() && RootNode->Transform.LayoutBoundsSize.IsSet())
			{
				const FVector2D SourceSize = RootNode->Transform.LayoutBoundsSize.GetValue();
				if (RootNode->Transform.Width.IsFit())
					RootNode->Transform.Width = FDesignSize::Fixed(SourceSize.X);
				if (RootNode->Transform.Height.IsFit())
					RootNode->Transform.Height = FDesignSize::Fixed(SourceSize.Y);
			}
		}
		TArray<FString> FontWarnings;
		if (!DesignLocalFontImport::Prepare(Document, FontWarnings, Context.Error, &FontSources)) return Fail();
		Component.Data = FDesign2UMGConverter::Convert(Document, Effective.ComponentConversionSettings);
		// Ordinary empty frames may be removed by conversion. A standalone empty
		// component instead needs a blank root that can accept its instance content.
		if (Document.Children.Num() == 1 && Document.Children[0]->Children.IsEmpty()
			&& Component.Data.RootWidget.Children.IsEmpty()
			&& Component.Data.RootWidget.Bounds.W == 0 && Component.Data.RootWidget.Bounds.H == 0
			&& Document.Children[0]->Transform.LayoutBoundsSize.IsSet())
		{
			const auto& SourceRoot = Document.Children[0];
			const FVector2D Size = SourceRoot->Transform.LayoutBoundsSize.GetValue();
			Component.Data.RootWidget.Id = SourceRoot->Id;
			Component.Data.RootWidget.Name = SourceRoot->Name;
			Component.Data.RootWidget.Bounds.W = Size.X;
			Component.Data.RootWidget.Bounds.H = Size.Y;
			Component.Data.RootWidget.MutableSource().SourceNodeId = SourceRoot->Id;
			Component.Data.RootWidget.MutableSource().SourceNodeType = TEXT("COMPONENT");
		}
		Component.Data.Warnings.Append(FontWarnings);
		Component.Data.WidgetName = StableName(Context.DocumentScope, Key);
		Component.Data.MutableSourceDocument().DocumentScope = Context.DocumentScope;
		Component.PackageFolder = Folder / TEXT("Components");
		const FString SetKey = Definition.ComponentSetKey.IsEmpty() ? Definition.ComponentSetId : Definition.ComponentSetKey;
		if (!SetKey.IsEmpty()) Component.PackageFolder /= StableName(Context.DocumentScope, SetKey);
		UWidgetBlueprint* ExistingComponent = nullptr;
		if (!ResolveExistingComponentFolder(ComponentFolders, Component.Data.WidgetName, Component.PackageFolder, Context.Error)
			|| !FindExisting(Component.Data, Component.PackageFolder, ExistingComponent, Context.Error)) return Fail(TEXT("component_asset_conflict"), EDesign2UMGCommitStatus::Conflict);
		Existing.Add(Key, ExistingComponent);
	}
	FUMGWidgetData RootData = Data;
	RootData.WidgetName = TargetBlueprint ? TargetBlueprint->GetName()
		: StableName(Context.DocumentScope, TEXT("root|") + Data.RootWidget.GetStableDesignNodeId());
	UWidgetBlueprint* ExistingRoot = nullptr;
	if (!FindExisting(RootData, Folder, ExistingRoot, Context.Error, false)) return Fail(TEXT("component_asset_conflict"), EDesign2UMGCommitStatus::Conflict);
	FDesign2UMGGenerationManifestSidecar RootManifest;
	if (ExistingRoot && (!Design2UMGGenerationManifest::TryLoadSidecarForAsset(
		ExistingRoot->GetOutermost()->GetName(), RootManifest, Context.Error)
		|| !ValidateExistingInstanceContent(ExistingRoot, RootManifest, Context.Error)))
		return Fail(TEXT("component_asset_conflict"), EDesign2UMGCommitStatus::Conflict);
	FDesign2UMGGenerationOutput Result;
	// Keep dependency and page edits in one editor transaction. It is closed
	// before Build returns; the output never owns a live editor transaction.
	const bool HasExistingAssets = ExistingRoot || Algo::AnyOf(Existing, [](const auto& Entry) { return Entry.Value != nullptr; });
	if (HasExistingAssets && GEditor && GEditor->CanTransact())
	{
		if (GEditor->IsTransactionActive())
		{
			Context.Error = TEXT("Finish the active editor transaction before importing a component batch");
			return Fail(TEXT("component_transaction_busy"), EDesign2UMGCommitStatus::Conflict);
		}
		BulkTransactionIndex = GEditor->BeginTransaction(TEXT("Design2UMG Component Bulk"),
			NSLOCTEXT("Design2UMG", "ComponentBulkTransaction", "Design2UMG Component Bulk"), ExistingRoot);
		TArray<UWidgetBlueprint*> ExistingAssets;
		Existing.GenerateValueArray(ExistingAssets);
		ExistingAssets.AddUnique(ExistingRoot);
		for (UWidgetBlueprint* Asset : ExistingAssets)
		{
			if (!Asset) continue;
			Asset->SetFlags(RF_Transactional);
			Asset->Modify();
			TArray<UObject*> TreeObjects;
			GetObjectsWithOuter(Asset->WidgetTree, TreeObjects, EGetObjectsFlags::IncludeNestedObjects);
			TreeObjects.Add(Asset->WidgetTree);
			for (UObject* Object : TreeObjects)
			{
				Object->SetFlags(RF_Transactional);
				Object->Modify();
			}
		}
	}
	for (const FString& Key : Order)
	{
		auto& Component = Context.Components[Key];
		FUMGWidgetData Template = Component.Data;
		ExposeContent(Template.RootWidget, TEXT("0"), Context);
		FDesign2UMGGeneratedComponent Generated;
		Generated.Output = MakeShared<FDesign2UMGGenerationOutput>();
		if (Existing[Key]) TouchedAssets.AddUnique(Existing[Key]);
		if (Existing[Key])
		{
			FDesign2UMGGenerationManifestSidecar Manifest;
			if (!Design2UMGGenerationManifest::TryLoadSidecarForAsset(Existing[Key]->GetOutermost()->GetName(), Manifest, Context.Error))
				return Fail();
			FDesignReimportRequest Request;
			Request.TargetAssetPath = Existing[Key]->GetOutermost()->GetName();
			const DesignImportPipeline::FReimportSession Session{nullptr, Request, Template, Effective};
			FUMGPrototypeNavigationData ComponentNavigation;
			int32 TransactionIndex = INDEX_NONE;
			const FDesignReimportResult Updated = DesignImportPipeline::ApplyPreparedReimport(
				Session, Existing[Key], Manifest, *Generated.Output, ComponentNavigation, TransactionIndex);
			if (TransactionIndex != INDEX_NONE) GEditor->EndTransaction();
			Generated.Output->Commit = Updated.Commit;
			Generated.Output->MappingDiagnostics.Append(Updated.Diagnostics);
			Generated.Output->MappingDiagnostics.Append(Updated.ComponentMappingDiagnostics);
			if (Updated.bSucceeded) Generated.Blueprint = Existing[Key];
		}
		else
		{
			// New assets are discarded on failure, rather than undone into empty blueprints.
			TGuardValue<ITransaction*> SuspendNewAssetUndo(GUndo, nullptr);
			Generated.Blueprint = FUMGGenerator::BuildWidgetBlueprint(Template, Component.PackageFolder, &Effective, Generated.Output.Get());
		}
		if (!Generated.Blueprint)
		{
			for (const FDesign2UMGMappingDiagnostic& Diagnostic : Generated.Output->MappingDiagnostics)
			{
				Output->MappingDiagnostics.Add({Diagnostic.Kind,
					FString::Printf(TEXT("Component '%s': %s"), *Key, *Diagnostic.Message)});
			}
			if (Context.Error.IsEmpty()) Context.Error = TEXT("Failed to build component: ") + Key;
			return Fail(TEXT("component_generation_failed"), Generated.Output->Commit.Status == EDesign2UMGCommitStatus::Conflict
				? EDesign2UMGCommitStatus::Conflict : EDesign2UMGCommitStatus::NotCommitted);
		}
		TouchedAssets.AddUnique(Generated.Blueprint);
		{
			// Compiler-created replacement objects are not authored widget edits.
			TGuardValue<ITransaction*> SuspendCompilerUndo(GUndo, nullptr);
			FKismetEditorUtilities::CompileBlueprint(Generated.Blueprint);
		}
		if (Generated.Blueprint->Status == BS_Error)
		{
			Context.Error = TEXT("Component compilation failed: ") + Key;
			return Fail();
		}
		Generated.Output->Manifest.bWholeAssetOwned = true;
		Component.ClassPath = Generated.Blueprint->GeneratedClass->GetPathName();
		UUserWidget* Defaults = CastChecked<UUserWidget>(Generated.Blueprint->GeneratedClass->GetDefaultObject());
		Defaults->DesignTimeSize = FVector2D(Component.Data.RootWidget.Bounds.W, Component.Data.RootWidget.Bounds.H);
		Defaults->DesignSizeMode = EDesignPreviewSizeMode::Custom;
		Result.Components.Add(MoveTemp(Generated));
	}
	if (ExistingRoot) TouchedAssets.AddUnique(ExistingRoot);
	UWidgetBlueprint* Root = nullptr;
	if (ExistingRoot)
	{
		if (RootData.RootWidget.Bounds.W <= 0.0f || RootData.RootWidget.Bounds.H <= 0.0f)
		{
			Context.Error = TEXT("source data did not provide valid root design size");
			Output->MappingDiagnostics.Add({TEXT("invalid_root_design_size"), FString::Printf(
				TEXT("Widget '%s' source node '%s' did not provide valid root design size"),
				*RootData.WidgetName, *RootData.RootWidget.GetStableDesignNodeId())});
			return Fail();
		}
		FDesignReimportRequest Request;
		Request.TargetAssetPath = ExistingRoot->GetOutermost()->GetName();
		const DesignImportPipeline::FReimportSession Session{nullptr, Request, RootData, Effective};
		FUMGPrototypeNavigationData UpdatedNavigation;
		FDesign2UMGGenerationOutput UpdatedOutput;
		int32 TransactionIndex = INDEX_NONE;
		const FDesignReimportResult Updated = DesignImportPipeline::ApplyPreparedReimport(
			Session, ExistingRoot, RootManifest, UpdatedOutput, UpdatedNavigation, TransactionIndex);
		if (TransactionIndex != INDEX_NONE) GEditor->EndTransaction();
		Result.MappingDiagnostics.Append(Updated.Diagnostics);
		Result.MappingDiagnostics.Append(Updated.ComponentMappingDiagnostics);
		if (!Updated.bSucceeded || !Context.Error.IsEmpty())
		{
			Output->MappingDiagnostics.Append(Updated.Diagnostics);
			Output->MappingDiagnostics.Append(Updated.ComponentMappingDiagnostics);
			if (Context.Error.IsEmpty()) Context.Error = TEXT("Page ownership merge failed: ") + Request.TargetAssetPath;
			return Fail(TEXT("component_asset_conflict"), Updated.bFullConflict
				? EDesign2UMGCommitStatus::Conflict : EDesign2UMGCommitStatus::NotCommitted);
		}
		Result.Manifest = MoveTemp(UpdatedOutput.Manifest);
		Result.bHasDegradation = UpdatedOutput.bHasDegradation;
		if (Navigation) *Navigation = MoveTemp(UpdatedNavigation);
		Root = ExistingRoot;
	}
	else
	{
		TGuardValue<ITransaction*> SuspendNewAssetUndo(GUndo, nullptr);
		Root = FUMGGenerator::BuildWidgetBlueprint(RootData, Folder, &Effective, &Result, Navigation);
	}
	// Root generation writes its own diagnostics. Append dependency diagnostics
	// afterwards so successful, off-page components are represented in the report.
	for (int32 Index = 0; Index < Order.Num(); ++Index)
	{
		const FString& Key = Order[Index];
		Result.bHasDegradation |= Result.Components[Index].Output->bHasDegradation;
		for (const FString& Warning : Context.Components[Key].Data.Warnings)
		{
			Result.MappingDiagnostics.Add({TEXT("component_warning"), FString::Printf(TEXT("Component '%s': %s"), *Key, *Warning)});
		}
		for (const FDesign2UMGMappingDiagnostic& Diagnostic : Result.Components[Index].Output->MappingDiagnostics)
		{
			Result.MappingDiagnostics.Add({Diagnostic.Kind, FString::Printf(TEXT("Component '%s': %s"), *Key, *Diagnostic.Message)});
		}
	}
	Result.Manifest.bComponentBulkRoot = true;
	Result.Commit.Memory = EDesign2UMGMemoryState::Modified;
	if (Root) Result.Commit.AssetPath = Root->GetPathName();
	EndBulkTransaction(Result);
	if (Output) *Output = MoveTemp(Result);
	if (!Root)
	{
		if (Context.Error.IsEmpty()) Context.Error = TEXT("Failed to build component batch root: ") + RootData.RootWidget.GetStableDesignNodeId();
		return Fail();
	}
	return Root;
}
