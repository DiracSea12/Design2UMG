// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGGenerator.h"
#include "Design2UMGGenerationSettingsSnapshot.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "UMGGeneratorRootHandoff.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGValueParsing.h"
#include "Design2UMGPrototypeNavigationTagHelper.h"
#include "UObject/SavePackage.h"
#include "Design2UMGFigmaMetadataExtensions.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "ObjectTools.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectHash.h"

namespace
{
	void ConfigureFigmaMetadataExtension(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGSourceDocumentData& DocumentData,
		const TArray<FDesign2UMGFigmaWidgetMetadataRecord>& WidgetRecords)
	{
		if (!WidgetBlueprint)
		{
			return;
		}

		if (DocumentData.HasAnyComponentCatalog() || WidgetRecords.Num() > 0)
		{
			UDesign2UMGFigmaMetadataBlueprintExtension* Extension =
				UWidgetBlueprintExtension::RequestExtension<UDesign2UMGFigmaMetadataBlueprintExtension>(WidgetBlueprint);
			Extension->SetMetadata(DocumentData, WidgetRecords);
			return;
		}

		if (UDesign2UMGFigmaMetadataBlueprintExtension* ExistingExtension =
			UWidgetBlueprintExtension::GetExtension<UDesign2UMGFigmaMetadataBlueprintExtension>(WidgetBlueprint))
		{
			WidgetBlueprint->RemoveExtension(ExistingExtension);
		}
	}

	void DumpTemplateTextBlockFontState(const TCHAR* Stage, UWidgetBlueprint* WidgetBlueprint)
	{
		if (!UE_LOG_ACTIVE(LogDesign2UMG, Verbose) || !WidgetBlueprint || !WidgetBlueprint->WidgetTree)
		{
			return;
		}

		WidgetBlueprint->WidgetTree->ForEachWidget([Stage, WidgetBlueprint](UWidget* Widget)
		{
			UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
			if (!TextBlock)
			{
				return;
			}

			const FSlateFontInfo FontInfo = TextBlock->GetFont();
			const UObject* FontObject = FontInfo.FontObject;
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("TextFont[%s]: blueprint='%s' widget='%s' text='%s' fontObject='%s' typeface='%s' size=%.1f tracking=%d wrap=%.1f autoWrap=%d"),
				Stage,
				*WidgetBlueprint->GetPathName(),
				*TextBlock->GetName(),
				*TextBlock->GetText().ToString(),
				FontObject ? *FontObject->GetPathName() : TEXT("<null>"),
				*FontInfo.TypefaceFontName.ToString(),
				FontInfo.Size,
				FontInfo.LetterSpacing,
				TextBlock->GetWrapTextAt(),
				TextBlock->GetAutoWrapText() ? 1 : 0);
		});
	}

	void ClearGeneratedWidgetNavigationState(UWidgetBlueprint* WidgetBlueprint)
	{
		if (!WidgetBlueprint)
		{
			return;
		}

		int32 ClearedWidgetCount = 0;
		TSet<UWidgetNavigation*> SanitizedNavigationObjects;
		auto ResetNavigationObject = [&SanitizedNavigationObjects](UWidgetNavigation* Navigation)
		{
			if (!Navigation || SanitizedNavigationObjects.Contains(Navigation))
			{
				return;
			}

			Navigation->Up = FWidgetNavigationData();
			Navigation->Down = FWidgetNavigationData();
			Navigation->Left = FWidgetNavigationData();
			Navigation->Right = FWidgetNavigationData();
			Navigation->Next = FWidgetNavigationData();
			Navigation->Previous = FWidgetNavigationData();
			Navigation->RoutingPolicy = EWidgetNavigationRoutingPolicy::Default;
			Navigation->NavigationMethod.Reset();
			SanitizedNavigationObjects.Add(Navigation);
		};

		auto ClearNavigation = [&ClearedWidgetCount, &ResetNavigationObject](UWidget* Widget)
		{
			if (!Widget || !Widget->Navigation)
			{
				return;
			}

			// Clear stale directional navigation payloads so UMG compile/validation
			// cannot resurrect old NavigationMethod state from previous template trees.
			ResetNavigationObject(Widget->Navigation);
			Widget->Navigation = nullptr;
			++ClearedWidgetCount;
		};

		auto ClearGeneratedClassNavigation = [&ClearNavigation](UClass* ClassObject)
		{
			if (UUserWidget* GeneratedCDO = ClassObject ? Cast<UUserWidget>(ClassObject->GetDefaultObject()) : nullptr)
			{
				ClearNavigation(GeneratedCDO);
				if (GeneratedCDO->WidgetTree)
				{
					GeneratedCDO->WidgetTree->ForEachWidget(ClearNavigation);
				}
			}
		};

		ClearGeneratedClassNavigation(WidgetBlueprint->GeneratedClass);
		ClearGeneratedClassNavigation(WidgetBlueprint->SkeletonGeneratedClass);

		if (WidgetBlueprint->WidgetTree)
		{
			WidgetBlueprint->WidgetTree->ForEachWidget(ClearNavigation);
		}

		if (UPackage* Package = WidgetBlueprint->GetOutermost())
		{
			ForEachObjectWithPackage(Package, [&ClearNavigation, &ResetNavigationObject](UObject* Object)
			{
				if (UWidget* Widget = Cast<UWidget>(Object))
				{
					ClearNavigation(Widget);
					return true;
				}

				if (UWidgetNavigation* Navigation = Cast<UWidgetNavigation>(Object))
				{
					ResetNavigationObject(Navigation);
				}

				return true;
			}, true, RF_Transient, EInternalObjectFlags::Garbage);
		}

		if (ClearedWidgetCount > 0 || SanitizedNavigationObjects.Num() > 0)
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Cleared stale widget navigation refs on %d widgets and sanitized %d navigation objects before compile"),
				ClearedWidgetCount,
				SanitizedNavigationObjects.Num());
		}
	}

	void ApplyDesignPreviewSize(UWidgetBlueprint* WBP, const FIntPoint& DesignSize, const TCHAR* Phase)
	{
		if (!WBP)
		{
			return;
		}

		UClass* GeneratedClass = WBP->GeneratedClass;
		if (!GeneratedClass)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ApplyDesignPreviewSize(%s): GeneratedClass is null"), Phase ? Phase : TEXT("unknown"));
			return;
		}

		UUserWidget* CDO = Cast<UUserWidget>(GeneratedClass->GetDefaultObject());
		if (!CDO)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ApplyDesignPreviewSize(%s): Failed to get UUserWidget CDO"), Phase ? Phase : TEXT("unknown"));
			return;
		}

		CDO->DesignTimeSize = FVector2D(DesignSize.X, DesignSize.Y);
		CDO->DesignSizeMode = EDesignPreviewSizeMode::Custom;
		UE_LOG(LogDesign2UMG, Log, TEXT("Set DesignTimeSize to %.0fx%.0f (mode=Custom, phase=%s)"),
			CDO->DesignTimeSize.X, CDO->DesignTimeSize.Y, Phase ? Phase : TEXT("unknown"));
	}

	FIntPoint ReadDesignPreviewSize(UWidgetBlueprint* WBP)
	{
		if (!WBP || !WBP->GeneratedClass)
		{
			return FIntPoint::ZeroValue;
		}

		if (UUserWidget* CDO = Cast<UUserWidget>(WBP->GeneratedClass->GetDefaultObject()))
		{
			const FVector2D CurrentSize = CDO->DesignTimeSize;
			if (CurrentSize.X > 1.0f && CurrentSize.Y > 1.0f)
			{
				return FIntPoint(
					FMath::CeilToInt(CurrentSize.X),
					FMath::CeilToInt(CurrentSize.Y));
			}
		}

		return FIntPoint::ZeroValue;
	}


}

UWidgetBlueprint* FUMGGenerator::GenerateWidgetBlueprint(
	const FUMGWidgetData& WidgetData,
	const FString& PackagePath,
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings)
{
	UWidgetBlueprint* WBP = BuildWidgetBlueprint(WidgetData, PackagePath, GenerationSettings);
	if (WBP)
		SaveWidgetBlueprint(WBP);
	return WBP;
}

UWidgetBlueprint* FUMGGenerator::BuildWidgetBlueprint(
	const FUMGWidgetData& WidgetData,
	const FString& PackagePath,
	const FDesign2UMGGenerationSettingsSnapshot* GenerationSettings)
{
	FString AssetName = ObjectTools::SanitizeObjectName(WidgetData.WidgetName);
	FString FullPath = PackagePath / AssetName;

	// Check for asset name collisions.
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FString TestPath = FullPath + TEXT(".") + AssetName;
	if (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
	{
		int32 Suffix = 1;
		FString NewName;
		constexpr int32 MaxAttempts = 1000;

		do {
			NewName = FString::Printf(TEXT("%s_%d"), *AssetName, Suffix);
			TestPath = PackagePath / NewName + TEXT(".") + NewName;
			++Suffix;
		} while (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid() && Suffix <= MaxAttempts);

		if (Suffix > MaxAttempts && AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to find unique name for asset '%s'"), *WidgetData.WidgetName);
			UE_LOG(LogDesign2UMG, Error, TEXT("Failed to generate a unique asset name for '%s'. Clean the output directory and retry."), *WidgetData.WidgetName);
			return nullptr;
		}

		AssetName = NewName;
		FullPath = PackagePath / AssetName;
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Asset renamed to %s"), *AssetName);
		UE_LOG(LogDesign2UMG, Warning, TEXT("Output path already contained an asset with this name; auto-renamed to %s."), *AssetName);
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create package at %s"), *FullPath);
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create the asset package. Check that the output path is valid: %s"), *FullPath);
		return nullptr;
	}

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), Package, FName(*AssetName),
			BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(), NAME_None
		)
	);

	if (!WBP || !WBP->WidgetTree)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create Widget Blueprint"));
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create the Widget Blueprint. Check engine-version compatibility."));
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}

	FUMGPrototypeNavigationData NavigationData = WidgetData.PrototypeNavigation();
	const FDesign2UMGGenerationSettingsSnapshot FallbackGenerationSettings =
		GenerationSettings
			? FDesign2UMGGenerationSettingsSnapshot()
			: FDesign2UMGGenerationSettingsSnapshot::FromSettings(*UDesign2UMGSettings::Get());
	const FDesign2UMGGenerationSettingsSnapshot& EffectiveGenerationSettings =
		GenerationSettings ? *GenerationSettings : FallbackGenerationSettings;
	const Design2UMGGenerationSettingsContext::FScopedSettingsSnapshot ScopedGenerationSettings(EffectiveGenerationSettings);

	const UMGGeneratorRootHandoff::FPreparedRoot PreparedRoot =
		UMGGeneratorRootHandoff::PrepareRootForBuild(WidgetData, PackagePath, AssetName);
	const FUMGWidgetNode& FixedRoot = PreparedRoot.Root;
	const FIntPoint& DesignSize = PreparedRoot.DesignSize;
	if (DesignSize.X <= 1 || DesignSize.Y <= 1)
	{
		UE_LOG(LogDesign2UMG, Error,
			TEXT("Failed to generate '%s': source data did not provide a valid root design size"),
			*WidgetData.WidgetName);
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}

	// Dump widget tree only in verbose mode.
	if (UE_LOG_ACTIVE(LogDesign2UMG, Verbose))
	{
		TFunction<void(const FUMGWidgetNode&, int32)> DumpNode = [&](const FUMGWidgetNode& Node, int32 Depth)
		{
			FString Indent;
			for (int32 i = 0; i < Depth; ++i) Indent += TEXT("  ");

			FString SlotInfo = FString::Printf(TEXT("slot[h=%s v=%s sz=%s fw=%.1f off=(%.0f,%.0f,%.0f,%.0f) anc=(%.2f,%.2f,%.2f,%.2f) aln=(%.2f,%.2f) auto=%d]"),
				FUMGValueParser::FormatSlotHorizontalAlignment(Node.Slot.HorizontalAlignment),
				FUMGValueParser::FormatSlotVerticalAlignment(Node.Slot.VerticalAlignment),
				FUMGValueParser::FormatSlotSizeRule(Node.Slot.SizeRule), Node.Slot.FillWeight,
				Node.Slot.Offsets.Left, Node.Slot.Offsets.Top, Node.Slot.Offsets.Right, Node.Slot.Offsets.Bottom,
				Node.Slot.Anchors.Min.X, Node.Slot.Anchors.Min.Y, Node.Slot.Anchors.Max.X, Node.Slot.Anchors.Max.Y,
				Node.Slot.Alignment.X, Node.Slot.Alignment.Y,
				Node.Slot.bAutoSize ? 1 : 0);

			FString PropsInfo;
			Design2UMGWidgetNodeAccess::ForEachStaticTypedWidgetPropertyKey([&PropsInfo, &Node](const TCHAR* Key)
			{
				FString Value;
				if (!Design2UMGWidgetNodeAccess::TryGetTypedWidgetString(Node, Key, Value)
					|| Value.IsEmpty()
					|| FCString::Strcmp(Key, TEXT("PathGeometry")) == 0
					|| FCString::Strcmp(Key, TEXT("MaterialParams")) == 0)
				{
					return;
				}
				PropsInfo += FString::Printf(TEXT(" %s=%s"), Key, *Value.Left(40));
			});

			UE_LOG(LogDesign2UMG, Verbose, TEXT("TREE: %s[%s] '%s' bounds=(%.1f,%.1f,%.1f,%.1f) %s%s"),
				*Indent, *Node.Type, *Node.Name,
				Node.Bounds.X, Node.Bounds.Y, Node.Bounds.W, Node.Bounds.H,
				*SlotInfo, *PropsInfo);

			for (const auto& Child : Node.Children)
				DumpNode(Child, Depth + 1);
		};
		UE_LOG(LogDesign2UMG, Verbose, TEXT("=== Widget Tree Dump (after Fixer) ==="));
		DumpNode(FixedRoot, 0);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("=== End Widget Tree Dump ==="));
	}

	// Use the shared widget creation path.
	TArray<FDesign2UMGFigmaWidgetMetadataRecord> FigmaMetadataRecords;
	UWidget* Root = UMGGeneratorRootHandoff::CreateRootWidget(WBP, PreparedRoot, FigmaMetadataRecords);
	if (!Root)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create root widget for '%s'"), *WidgetData.WidgetName);
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create root widget '%s'. The design may contain unsupported node types."), *WidgetData.WidgetName);
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}
	WBP->WidgetTree->RootWidget = Root;
	switch (EffectiveGenerationSettings.PrototypeNavigationGenerationMode)
	{
	case EDesign2UMGPrototypeNavigationGenerationMode::RuntimeExtension:
		UMGPrototypeAnimationBuilder::GenerateNavigationAnimations(WBP, FixedRoot, NavigationData);
		UMGPrototypeAnimationBuilder::ConfigureNavigationExtension(WBP, NavigationData);
		break;
	case EDesign2UMGPrototypeNavigationGenerationMode::TagHelper:
		Design2UMGPrototypeNavigationTagHelper::ConfigureMetadata(WBP, NavigationData);
		break;
	case EDesign2UMGPrototypeNavigationGenerationMode::Disabled:
		break;
	default:
		break;
	}
	ConfigureFigmaMetadataExtension(WBP, WidgetData.SourceDocument(), FigmaMetadataRecords);
	ClearGeneratedWidgetNavigationState(WBP);
	DumpTemplateTextBlockFontState(TEXT("template-precompile"), WBP);

	// Push the editor preview size into the CDO. SaveWidgetBlueprint writes it back
	// after compilation so the compile step cannot reset the default value.
	ApplyDesignPreviewSize(WBP, DesignSize, TEXT("pre-compile"));

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(WBP);

	UE_LOG(LogDesign2UMG, Log, TEXT("Built Widget Blueprint at %s (not yet compiled/saved)"), *FullPath);
	return WBP;
}

void FUMGGenerator::SaveWidgetBlueprint(UWidgetBlueprint* WBP)
{
	if (!WBP) return;

	FIntPoint DesiredDesignSize = ReadDesignPreviewSize(WBP);
	if (DesiredDesignSize.X <= 1 || DesiredDesignSize.Y <= 1)
	{
		UE_LOG(LogDesign2UMG, Error,
			TEXT("SaveWidgetBlueprint: missing valid DesignTimeSize for %s; source design size is required"),
			*WBP->GetPathName());
		return;
	}

	// Compile after rendering; transient texture data may already be gone at this point.
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	// MarkBlueprintAsStructurallyModified can populate the GUID map during an
	// intermediate compile. Clear it so CompileBlueprint rebuilds from the widget tree
	// instead of carrying stale entries forward.
	WBP->WidgetVariableNameToGuidMap.Empty();
	ClearGeneratedWidgetNavigationState(WBP);
	const EBlueprintCompileOptions CompileOptions =
		EBlueprintCompileOptions::SkipDefaultObjectValidation |
		EBlueprintCompileOptions::SkipFiBSearchMetaUpdate;
	FKismetEditorUtilities::CompileBlueprint(WBP, CompileOptions);
	DumpTemplateTextBlockFontState(TEXT("template-postcompile"), WBP);
	ApplyDesignPreviewSize(WBP, DesiredDesignSize, TEXT("post-compile"));

	UPackage* Package = WBP->GetOutermost();
	FString FullPath = Package->GetName();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString FileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, WBP, *FileName, SaveArgs);

	UE_LOG(LogDesign2UMG, Log, TEXT("Saved Widget Blueprint at %s"), *FullPath);
}
