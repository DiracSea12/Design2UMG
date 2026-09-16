// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Automation/Design2UMGAutomationRender.h"

#include "Automation/Design2UMGAutomationGeometryDump.h"
#include "Automation/Design2UMGAutomationIrDump.h"
#include "Automation/Design2UMGAutomationStructureSnapshot.h"
#include "Design2UMGLocalFigmaImport.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "DesignImportPipeline.h"
#include "DesignImportUtils.h"
#include "DesignJsonUtils.h"
#include "DesignPathRasterizer.h"
#include "DesignSchema.h"
#include "FigmaParser.h"
#include "FontResolver.h"
#include "ExternalServices/PenMCPBridge.h"
#include "ProceduralShapeGenerator.h"
#include "UMGSchema.h"
#include "UMGWidgetUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Texture2D.h"
#include "WidgetBlueprint.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontCache.h"
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "RenderingThread.h"
#include "Rendering/SlateRenderer.h"
#include "Serialization/JsonSerializer.h"
#include "TextureCompiler.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	// Test-only render-automation state; compiled out of non-test builds where
	// IsAutomationRenderActive() is a constant false.
#if WITH_DEV_AUTOMATION_TESTS
	bool GDesign2UMGAutomationRenderActive = false;
	FString GDesign2UMGAutomationFontImportPath;
#endif
	const TCHAR* AssetPackageReloadAuthority = TEXT("ue_reload_package_before_asset_render_v1");

	struct FAutomationRenderSpec
	{
		FString Mode;
		FString SourcePath;
		FString NodeId;
		FString OutputPath;
		FString RenderPngPath;
		FString ResultJsonPath;
		FString IrDumpPath;
		FString GeometryDumpPath;
		bool bForceReloadPackage = false;
		TOptional<FVector2D> RenderSizeOverride;
		TOptional<FVector2D> RootExportOffset;
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
		FIntPoint DesignTimeSize = FIntPoint::ZeroValue;
		bool bRenderSizeOverridden = false;
		TSharedPtr<FJsonObject> SourceStructure;
		TSharedPtr<FJsonObject> AssetStructure;
	};

	const TCHAR* GeneratedAssetFromResultPrefix = TEXT("@generated_asset_from:");

	// Parses a [number, number] JSON field (render size override, root export
	// offset); scalar validation stays with the helper, unit clamping with the
	// caller that knows what the pair means.
	bool TryGetNumberPairField(const TSharedPtr<FJsonObject>& Root, const TCHAR* Key, double& OutX, double& OutY)
	{
		const TSharedPtr<FJsonValue> Field = Root->TryGetField(Key);
		if (!Field.IsValid() || Field->Type != EJson::Array)
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Values = Field->AsArray();
		if (Values.Num() != 2 || Values[0]->Type != EJson::Number || Values[1]->Type != EJson::Number)
		{
			return false;
		}
		OutX = Values[0]->AsNumber();
		OutY = Values[1]->AsNumber();
		return true;
	}

	bool ShouldSkipDesignerOnlyNodeForAutomationSnapshot(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		switch (Node->Type)
		{
		case EDesignNodeType::Connection:
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
		if (!DesignJsonUtils::TryParseObject(JsonStr, Root))
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
		Root->TryGetStringField(TEXT("ir_dump_path"), OutSpec.IrDumpPath);
		Root->TryGetStringField(TEXT("geometry_dump_path"), OutSpec.GeometryDumpPath);
		Root->TryGetBoolField(TEXT("force_reload_package"), OutSpec.bForceReloadPackage);

		// Optional canvas calibration: [width, height] pinning the render surface
		// to the authoritative reference size (compare scope only).
		double OverrideWidth = 0.0;
		double OverrideHeight = 0.0;
		if (TryGetNumberPairField(Root, TEXT("render_size_override"), OverrideWidth, OverrideHeight))
		{
			OutSpec.RenderSizeOverride = FVector2D(
				static_cast<float>(FMath::Max(1, FMath::RoundToInt(OverrideWidth))),
				static_cast<float>(FMath::Max(1, FMath::RoundToInt(OverrideHeight))));
		}

		// Optional root export offset: [x, y] locating the node box inside
		// Pencil's node-export canvas (union of the node box and its outer
		// shadows). Pins the calibrated placement; also proves the calibration
		// canvas carries union padding.
		double OffsetX = 0.0;
		double OffsetY = 0.0;
		if (TryGetNumberPairField(Root, TEXT("render_root_offset"), OffsetX, OffsetY))
		{
			OutSpec.RootExportOffset = FVector2D(static_cast<float>(OffsetX), static_cast<float>(OffsetY));
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
		Root->SetNumberField(TEXT("design_time_width"), Result.DesignTimeSize.X);
		Root->SetNumberField(TEXT("design_time_height"), Result.DesignTimeSize.Y);
		Root->SetStringField(
			TEXT("render_size_authority"),
			Result.bRenderSizeOverridden
				? TEXT("reference_canvas_calibration")
				: TEXT("ue_blueprint_design_time_size"));
		Root->SetStringField(TEXT("pipeline_determinism_rule"), TEXT("source_pipeline_transient_state_v4"));
		Root->SetStringField(TEXT("transient_state_authority"), TEXT("ue_automation_render_reset"));
		Root->SetBoolField(TEXT("transient_state_reset_before_render"), true);
		Root->SetStringField(TEXT("text_measurement_cache_scope"), TEXT("per_automation_render"));
		Root->SetStringField(TEXT("text_single_line_height_probe_authority"), TEXT("slate_font_measure_single_line_height_v1"));
		Root->SetStringField(TEXT("slate_font_cache_authority"), TEXT("flush_before_automation_render_v1"));
		Root->SetBoolField(TEXT("force_reload_package_requested"), Spec.bForceReloadPackage);
		Root->SetStringField(
			TEXT("asset_package_reload_authority"),
			Spec.bForceReloadPackage ? AssetPackageReloadAuthority : TEXT("not_requested"));
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
		if (!DesignJsonUtils::TryParseObject(JsonStr, Root))
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
		if (!DesignJsonUtils::TryParseObject(PenJson, RootObject))
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

		OutAnalysis = DesignImportPipeline::AnalyzeImport(OutDocument, FImportAnalysisInput{ true });
		if (!OutAnalysis.ErrorMessage.IsEmpty())
		{
			OutError = OutAnalysis.ErrorMessage;
			return false;
		}
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
		if (FSlateApplication::IsInitialized())
		{
			if (FSlateRenderer* SlateRenderer = FSlateApplication::Get().GetRenderer())
			{
				SlateRenderer->FlushFontCache(TEXT("Design2UMG automation transient reset"));
				// FlushFontCache only queues a request. Complete the reset before GC
				// and before measuring fonts for the next source.
				SlateRenderer->GetFontCache()->ConditionalFlushCache();
			}
		}
		FFontResolver::ClearCache();
		FDesignPathRasterizer::ClearCache();
		FProceduralShapeGenerator::ReleaseTransientTextures();
		CollectGarbage(RF_NoFlags, true);
	}

	void PrepareAutomationTransientState()
	{
		ResetAutomationTransientState();
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

	bool ReloadAssetPackageForAutomationRender(const FString& ObjectPath, FString& OutError)
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		if (PackageName.IsEmpty())
		{
			OutError = FString::Printf(TEXT("Failed to resolve package name from asset path: %s"), *ObjectPath);
			return false;
		}

		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			return true;
		}

		UE_LOG(LogDesign2UMG, Log,
			TEXT("AutomationRender: force reloading package before asset render: %s"),
			*PackageName);
		FlushAsyncLoading();
		ResetLoaders(static_cast<UObject*>(Package));
		if (!ReloadPackage(Package, LOAD_None))
		{
			OutError = FString::Printf(TEXT("Failed to reload package before asset render: %s"), *PackageName);
			return false;
		}

		FlushRenderingCommands();
		CollectGarbage(RF_NoFlags, true);
		return true;
	}

	FString NormalizeAutomationPackagePath(const FString& PackagePath)
	{
		FString Normalized = PackagePath.TrimStartAndEnd();
		Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Normalized.EndsWith(TEXT("/")))
		{
			Normalized = Normalized.LeftChop(1);
		}
		return Normalized;
	}

	bool IsDefaultAutomationPackagePath(const FString& PackagePath)
	{
		return NormalizeAutomationPackagePath(PackagePath)
			.StartsWith(TEXT("/Game/Design2UMG/Automation/"), ESearchCase::CaseSensitive);
	}

	bool IsAutomationImportedFontPath(const FString& FontAssetPath)
	{
		const FString NormalizedPath = NormalizeAutomationPackagePath(FontAssetPath);
		return NormalizedPath.Contains(TEXT("/Design2UMG/Automation/"), ESearchCase::CaseSensitive)
			&& NormalizedPath.Contains(TEXT("/ImportedFonts/"), ESearchCase::CaseSensitive);
	}

	bool RefreshAutomationPackagesInDirectory(const FString& PackagePath, const TCHAR* Description, FString& OutError)
	{
		const FString NormalizedPath = NormalizeAutomationPackagePath(PackagePath);
		const FString PackageDirectory = FPackageName::LongPackageNameToFilename(NormalizedPath, TEXT(""));
		if (PackageDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*PackageDirectory))
		{
			return true;
		}

		TArray<FString> AssetFiles;
		IFileManager::Get().FindFiles(AssetFiles, *(PackageDirectory / TEXT("*.uasset")), true, false);
		if (AssetFiles.Num() == 0)
		{
			return true;
		}

		FlushAsyncLoading();
		for (const FString& AssetFile : AssetFiles)
		{
			const FString AssetName = FPaths::GetBaseFilename(AssetFile);
			if (AssetName.IsEmpty())
			{
				continue;
			}

			const FString PackageName = NormalizeAutomationPackagePath(NormalizedPath / AssetName);
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				Package = LoadPackage(nullptr, *PackageName, LOAD_None);
				if (!Package)
				{
					OutError = FString::Printf(
						TEXT("Failed to load %s package before asset render: %s"),
						Description,
						*PackageName);
					return false;
				}
			}
			else
			{
				ResetLoaders(static_cast<UObject*>(Package));
				if (!ReloadPackage(Package, LOAD_None))
				{
					OutError = FString::Printf(
						TEXT("Failed to reload %s package before asset render: %s"),
						Description,
						*PackageName);
					return false;
				}
			}

			if (UTexture2D* Texture = FindObject<UTexture2D>(Package, *AssetName))
			{
				Texture->ConditionalPostLoad();
#if WITH_EDITOR
				FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif
				Texture->UpdateResource();
			}
		}

		FlushRenderingCommands();
		CollectGarbage(RF_NoFlags, true);
		return true;
	}

	bool RefreshAutomationImportedFontPackages(const FString& FontImportPath, FString& OutError)
	{
		const FString NormalizedPath = NormalizeAutomationPackagePath(FontImportPath);
		if (!IsAutomationImportedFontPath(NormalizedPath))
		{
			return true;
		}

		return RefreshAutomationPackagesInDirectory(NormalizedPath, TEXT("automation imported font"), OutError);
	}

	bool RefreshAutomationTextureResourcesInDirectory(const FString& PackagePath, const TCHAR* Description, FString& OutError)
	{
		const FString NormalizedPath = NormalizeAutomationPackagePath(PackagePath);
		const FString PackageDirectory = FPackageName::LongPackageNameToFilename(NormalizedPath, TEXT(""));
		if (PackageDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*PackageDirectory))
		{
			return true;
		}

		TArray<FString> AssetFiles;
		IFileManager::Get().FindFiles(AssetFiles, *(PackageDirectory / TEXT("*.uasset")), true, false);
		if (AssetFiles.Num() == 0)
		{
			return true;
		}

		FlushAsyncLoading();
		for (const FString& AssetFile : AssetFiles)
		{
			const FString AssetName = FPaths::GetBaseFilename(AssetFile);
			if (AssetName.IsEmpty())
			{
				continue;
			}

			const FString PackageName = NormalizeAutomationPackagePath(NormalizedPath / AssetName);
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				Package = LoadPackage(nullptr, *PackageName, LOAD_None);
				if (!Package)
				{
					OutError = FString::Printf(
						TEXT("Failed to load %s package before asset render: %s"),
						Description,
						*PackageName);
					return false;
				}
			}

			UTexture2D* Texture = FindObject<UTexture2D>(Package, *AssetName);
			if (!Texture)
			{
				const FString ObjectPath = PackageName + TEXT(".") + AssetName;
				Texture = LoadObject<UTexture2D>(nullptr, *ObjectPath);
			}
			if (Texture)
			{
				Texture->ConditionalPostLoad();
#if WITH_EDITOR
				FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif
				Texture->UpdateResource();
			}
		}

		FlushRenderingCommands();
		CollectGarbage(RF_NoFlags, true);
		return true;
	}

	FString ResolveAutomationFontImportPath(const FString& OutputPath)
	{
		const FString NormalizedOutputPath = NormalizeAutomationPackagePath(OutputPath);
		if (NormalizedOutputPath.IsEmpty())
		{
			return TEXT("/Game/Design2UMG/Automation/ImportedFonts");
		}

		const FString ParentPath = FPaths::GetPath(NormalizedOutputPath);
		const FString ImportRoot = ParentPath.IsEmpty() ? NormalizedOutputPath : ParentPath;
		return NormalizeAutomationPackagePath(ImportRoot / TEXT("ImportedFonts"));
	}

	bool RefreshGeneratedTexturePackagesForAutomationRender(const FString& GeneratedAssetPath, FString& OutError)
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(GeneratedAssetPath);
		if (PackageName.IsEmpty())
		{
			return true;
		}

		const FString PackagePath = FPaths::GetPath(PackageName);
		return RefreshAutomationTextureResourcesInDirectory(PackagePath / TEXT("D2U_PathTextures"), TEXT("automation path texture"), OutError)
			&& RefreshAutomationTextureResourcesInDirectory(PackagePath / TEXT("D2U_ProceduralTextures"), TEXT("automation procedural texture"), OutError)
			&& RefreshAutomationTextureResourcesInDirectory(PackagePath / TEXT("D2U_ImageTransforms"), TEXT("automation image transform texture"), OutError);
	}

	bool ClearDefaultAutomationOutputAssets(const FString& OutputPath, FString& OutError)
	{
		const FString NormalizedOutputPath = NormalizeAutomationPackagePath(OutputPath);
		if (NormalizedOutputPath.IsEmpty() || !IsDefaultAutomationPackagePath(NormalizedOutputPath))
		{
			return true;
		}

		FAssetRegistryModule& AssetRegistryModule =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*NormalizedOutputPath), Assets, true);
		if (Assets.IsEmpty())
		{
			return true;
		}

		TArray<UObject*> ObjectsToDelete;
		ObjectsToDelete.Reserve(Assets.Num());
		for (const FAssetData& Asset : Assets)
		{
			if (UObject* Object = Asset.GetAsset())
			{
				ObjectsToDelete.Add(Object);
			}
		}

		if (ObjectsToDelete.IsEmpty())
		{
			return true;
		}

		UE_LOG(LogDesign2UMG, Log,
			TEXT("AutomationRender: clearing %d stale automation assets under %s"),
			ObjectsToDelete.Num(),
			*NormalizedOutputPath);
		ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
		FlushAsyncLoading();
		FlushRenderingCommands();
		CollectGarbage(RF_NoFlags, true);
		return true;
	}

	bool PrepareAutomationOutputPathForGeneration(const FAutomationRenderSpec& Spec, FString& OutError)
	{
		if (Spec.Mode.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
		{
			return true;
		}

		return ClearDefaultAutomationOutputAssets(Spec.OutputPath, OutError)
			&& ClearDefaultAutomationOutputAssets(ResolveAutomationFontImportPath(Spec.OutputPath), OutError);
	}

	FIntPoint ResolveBlueprintDesignTimeSize(UWidgetBlueprint* WidgetBlueprint)
	{
		const TOptional<FVector2D> DesignTimeSize = FUMGWidgetUtils::GetBlueprintDesignTimeSize(WidgetBlueprint);
		if (DesignTimeSize.IsSet())
		{
			return FIntPoint(
				FMath::CeilToInt(DesignTimeSize->X),
				FMath::CeilToInt(DesignTimeSize->Y));
		}

		return FIntPoint::ZeroValue;
	}

	bool RunAutomationRender(const FAutomationRenderSpec& Spec, FAutomationRenderResult& OutResult)
	{
#if WITH_DEV_AUTOMATION_TESTS
		const TGuardValue<FString> FontImportPathGuard(
			GDesign2UMGAutomationFontImportPath,
			ResolveAutomationFontImportPath(Spec.OutputPath));
		const TGuardValue<FString> IrDumpPathGuard(
			Design2UMGAutomationIrDump::GDesign2UMGAutomationIrDumpPath,
			Spec.IrDumpPath);
		const TGuardValue<FString> GeometryDumpPathGuard(
			Design2UMGAutomationGeometryDump::GDesign2UMGAutomationGeometryDumpPath,
			Spec.GeometryDumpPath);
#endif
		FDesign2UMGImportSettingsSnapshot AutomationSettingsSnapshot =
			FDesign2UMGImportSettingsSnapshot::FromSettings(UDesign2UMGSettings::Get());
		Design2UMGAutomationRender::SanitizeImportSettingsSnapshot(AutomationSettingsSnapshot);

		auto MakeAutomationImportRequest = [&](const bool bResolveImages)
		{
			FDesignImportRequest Request;
			Request.OutputPath = Spec.OutputPath;
			Request.bUsePreview = false;
			Request.bResolveImages = bResolveImages;
			Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintAndCompiledPng;
			Request.Generation.CompiledPngFilePath = Spec.RenderPngPath;
			Request.SettingsSnapshot = AutomationSettingsSnapshot;
			return Request;
		};

		auto ApplyExecutionResult = [&](const FDesignImportExecutionResult& ExecutionResult)
		{
			OutResult.GeneratedAssetPath = ExecutionResult.GeneratedAssetPath;
			OutResult.RenderPngPath = ExecutionResult.RenderPngPath.IsEmpty()
				? Spec.RenderPngPath
				: ExecutionResult.RenderPngPath;
			OutResult.RenderSize = ExecutionResult.RenderedSize;
			if (!ExecutionResult.bSucceeded && !ExecutionResult.ErrorMessage.IsEmpty())
			{
				OutResult.ErrorMessage = ExecutionResult.ErrorMessage;
			}
			return ExecutionResult.bSucceeded;
		};

		auto ApplyGenerationResult = [&](const FArtifactGenerationResult& GenerationResult)
		{
			OutResult.GeneratedAssetPath = GenerationResult.GeneratedAssetPath;
			OutResult.RenderPngPath = GenerationResult.RenderPngPath.IsEmpty()
				? Spec.RenderPngPath
				: GenerationResult.RenderPngPath;
			OutResult.RenderSize = GenerationResult.RenderedSize;
			if (!GenerationResult.bSucceeded && !GenerationResult.ErrorMessage.IsEmpty())
			{
				OutResult.ErrorMessage = GenerationResult.ErrorMessage;
			}
			return GenerationResult.bSucceeded;
		};

		auto CaptureGeneratedAssetStructure = [&]()
		{
			if (!OutResult.GeneratedAssetPath.IsEmpty())
			{
				if (UWidgetBlueprint* GeneratedBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *OutResult.GeneratedAssetPath))
				{
					OutResult.AssetStructure = Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(GeneratedBlueprint->WidgetTree);
					OutResult.DesignTimeSize = ResolveBlueprintDesignTimeSize(GeneratedBlueprint);
				}
			}
		};

		OutResult.Mode = Spec.Mode;
		OutResult.SourcePath = Spec.SourcePath;
		OutResult.RenderPngPath = Spec.RenderPngPath;
		UE_LOG(LogDesign2UMG, Log,
			TEXT("AutomationRender: mode=%s source=%s node=%s output=%s png=%s"),
			*Spec.Mode, *Spec.SourcePath, *Spec.NodeId, *Spec.OutputPath, *Spec.RenderPngPath);

		if (!PrepareAutomationOutputPathForGeneration(Spec, OutResult.ErrorMessage))
		{
			return false;
		}

		UWidgetBlueprint* WidgetBlueprint = nullptr;
		if (Spec.Mode.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
		{
			const FString ObjectPath = NormalizeWidgetBlueprintObjectPath(Spec.SourcePath);
			if (!Design2UMGAutomationRender::PrepareGeneratedAssetForAutomationRender(
				ObjectPath,
				Spec.bForceReloadPackage,
				OutResult.ErrorMessage))
			{
				return false;
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: loading asset %s"), *ObjectPath);
			WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
			if (!WidgetBlueprint)
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to load Widget Blueprint: %s"), *ObjectPath);
				return false;
			}

		OutResult.GeneratedAssetPath = WidgetBlueprint->GetPathName();
		OutResult.DesignTimeSize = ResolveBlueprintDesignTimeSize(WidgetBlueprint);
		OutResult.AssetStructure = Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(WidgetBlueprint->WidgetTree);
		// Second-stage calibration check: the blueprint's design-time extent is now
		// known. When it is already aspect-compatible with the calibrated (reference)
		// extent and the calibration is not union-padded, the legacy pipeline compares
		// fine on its own; skip the override to keep passing cases byte-identical to
		// the non-calibrated path. A calibration canvas significantly larger than the
		// design-time extent on any axis carries content-union padding (outer shadows,
		// overflowing children) that the design-time canvas cannot host, so it applies
		// even when the aspects match.
		TOptional<FVector2D> EffectiveRenderSizeOverride = Spec.RenderSizeOverride;
		if (EffectiveRenderSizeOverride.IsSet() && OutResult.DesignTimeSize.X > 1 && OutResult.DesignTimeSize.Y > 1)
		{
			const FVector2D OverrideSize = EffectiveRenderSizeOverride.GetValue();
			const int32 OverrideWidth = FMath::RoundToInt(OverrideSize.X);
			const int32 OverrideHeight = FMath::RoundToInt(OverrideSize.Y);
			const float DesignAspect = static_cast<float>(OutResult.DesignTimeSize.X) / OutResult.DesignTimeSize.Y;
			const float OverrideAspect = OverrideSize.X / OverrideSize.Y;
			const float AspectDelta = FMath::Abs(DesignAspect - OverrideAspect) / FMath::Max(DesignAspect, OverrideAspect);
			constexpr float ReferenceAspectCompatibilityThreshold = 0.08f;
			constexpr float ReferenceUnionPaddingScaleThreshold = 1.25f;
			const bool bAspectCompatible = AspectDelta <= ReferenceAspectCompatibilityThreshold;
			// A source-derived root export offset proves the reference canvas carries
			// union padding (outer-shadow padding can stay far below the 1.25x scale
			// heuristic), so the override applies even when aspects match.
			const bool bCalibrationCarriesUnionPadding =
				Spec.RootExportOffset.IsSet()
				|| OverrideSize.X > OutResult.DesignTimeSize.X * ReferenceUnionPaddingScaleThreshold
				|| OverrideSize.Y > OutResult.DesignTimeSize.Y * ReferenceUnionPaddingScaleThreshold;
			if (bAspectCompatible && !bCalibrationCarriesUnionPadding)
			{
				EffectiveRenderSizeOverride.Reset();
				UE_LOG(LogDesign2UMG, Log,
					TEXT("AutomationRender: design-time %dx%d is aspect-compatible with calibration %dx%d; calibration skipped"),
					OutResult.DesignTimeSize.X, OutResult.DesignTimeSize.Y, OverrideWidth, OverrideHeight);
			}
		}
		OutResult.bRenderSizeOverridden = EffectiveRenderSizeOverride.IsSet();
		if (!DesignImportUtils::RenderCompiledBlueprintToPngFile(
			WidgetBlueprint,
			Spec.RenderPngPath,
			&OutResult.RenderSize,
			&OutResult.ErrorMessage,
			EffectiveRenderSizeOverride,
			Spec.RootExportOffset))
			{
				if (OutResult.ErrorMessage.IsEmpty())
				{
					OutResult.ErrorMessage = FString::Printf(TEXT("Failed to render compiled Widget Blueprint: %s"), *ObjectPath);
				}
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
				if (OutResult.ErrorMessage.IsEmpty())
				{
					OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
				}
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

			if (!FPenMCPBridge::ConvertFromMCP(
				NodesJson,
				VariablesJson,
				AutomationSettingsSnapshot,
				Data,
				Spec.NodeId))
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to convert MCP payload for %s"), *Spec.SourcePath);
				return false;
			}

			OutResult.SourceStructure = Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(
				SourceDocument,
				Spec.NodeId,
				ShouldSkipDesignerOnlyNodeForAutomationSnapshot);
			FImportAnalysisInput AnalysisInput;
			AnalysisInput.bResolveImages = true;
			AnalysisInput.SettingsSnapshot = &AutomationSettingsSnapshot;
			const FImportAnalysisResult AnalysisResult = DesignImportPipeline::AnalyzeImport(Data, AnalysisInput);
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
				if (OutResult.ErrorMessage.IsEmpty())
				{
					OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
				}
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
				if (OutResult.ErrorMessage.IsEmpty())
				{
					OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
				}
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

		if (!ApplyGenerationResult(
			DesignImportPipeline::GenerateArtifacts(
				Data,
				GenerationRequest,
				AutomationSettingsSnapshot.GenerationSettings)))
		{
			if (OutResult.ErrorMessage.IsEmpty())
			{
				OutResult.ErrorMessage = FString::Printf(TEXT("Failed to generate compiled PNG for %s"), *Spec.SourcePath);
			}
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
#if WITH_DEV_AUTOMATION_TESTS
		return GDesign2UMGAutomationRenderActive;
#else
		return false;
#endif
	}

	bool PrepareGeneratedAssetForAutomationRender(
		const FString& GeneratedAssetPath,
		const bool bForceReloadPackage,
		FString& OutError)
	{
		if (!IsAutomationRenderActive())
		{
			return true;
		}

#if WITH_DEV_AUTOMATION_TESTS
		ResetAutomationTransientState();
		if (!RefreshAutomationImportedFontPackages(GDesign2UMGAutomationFontImportPath, OutError))
		{
			return false;
		}
#endif

		if (!bForceReloadPackage)
		{
			return true;
		}

		const FString ObjectPath = NormalizeWidgetBlueprintObjectPath(GeneratedAssetPath);
		if (!RefreshGeneratedTexturePackagesForAutomationRender(ObjectPath, OutError)
			|| !ReloadAssetPackageForAutomationRender(ObjectPath, OutError))
		{
			return false;
		}

		// Package refresh and reload populate render-related caches after the initial reset.
		// Clear them once more so loading the asset below starts from the same state for every case.
		ResetAutomationTransientState();
		return true;
	}

	void SanitizeImportSettingsSnapshot(FDesign2UMGImportSettingsSnapshot& InOutSettingsSnapshot)
	{
		TArray<FString> FamiliesToRemove;
		for (const TPair<FString, FString>& Pair : InOutSettingsSnapshot.ConversionSettings.IconFontAssetPaths)
		{
			if (DesignImportUtils::IsCaseLocalAutomationImportedFontPath(Pair.Value))
			{
				FamiliesToRemove.Add(Pair.Key);
			}
		}

		for (const FString& Family : FamiliesToRemove)
		{
			InOutSettingsSnapshot.ConversionSettings.IconFontAssetPaths.Remove(Family);
		}

		if (DesignImportUtils::IsCaseLocalAutomationImportedFontPath(InOutSettingsSnapshot.ConversionSettings.DefaultIconFontAssetPath))
		{
			InOutSettingsSnapshot.ConversionSettings.DefaultIconFontAssetPath.Reset();
		}
	}

	void HandleConsoleCommand(const TArray<FString>& Args)
	{
#if WITH_DEV_AUTOMATION_TESTS
		TGuardValue<bool> AutomationGuard(GDesign2UMGAutomationRenderActive, true);
#endif
		FAutomationRenderResult Result;
		Result.Mode = TEXT("unknown");
		const bool bSuppressExitAfterCompletion = Args.ContainsByPredicate([](const FString& Arg)
		{
			return Arg.Equals(TEXT("--no-quit"), ESearchCase::IgnoreCase)
				|| Arg.Equals(TEXT("no-quit"), ESearchCase::IgnoreCase);
		});
		const bool bRequestExitAfterCompletion = !bSuppressExitAfterCompletion
			&& Args.ContainsByPredicate([](const FString& Arg)
			{
				return Arg.Equals(TEXT("--quit"), ESearchCase::IgnoreCase)
					|| Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase);
			});
		const bool bShouldExitAfterCompletion = bRequestExitAfterCompletion
			|| (!bSuppressExitAfterCompletion && FApp::IsUnattended());

		if (Args.Num() < 1)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Usage: Design2UMG.AutomationRender <spec.json>"));
			if (bShouldExitAfterCompletion)
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
			if (bShouldExitAfterCompletion)
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
			if (bShouldExitAfterCompletion)
			{
				FPlatformMisc::RequestExitWithStatus(false, 1);
			}
			return;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("AutomationRender: spec loaded successfully"));
		PrepareAutomationTransientState();
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

		if (bShouldExitAfterCompletion)
		{
			FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
		}
	}
}


#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesign2UMGFontResetSynchronousTest,
	"Design2UMG.Automation.FontResetCompletesSynchronously",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGFontResetSynchronousTest::RunTest(const FString&)
{
	FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
	Renderer->FlushFontCache(TEXT("Font reset regression test"));
	ResetAutomationTransientState();
	return TestFalse(TEXT("No pending font flush remains before parsing the next case"),
		Renderer->GetFontCache()->ConditionalFlushCache());
}
#endif
