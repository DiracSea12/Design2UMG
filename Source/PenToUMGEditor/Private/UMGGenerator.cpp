// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "PenToUMGLog.h"
#include "PenToUMGSettings.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

UWidgetBlueprint* FUMGGenerator::GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
{
	FString AssetName = WidgetData.WidgetName;
	FString FullPath = PackagePath / AssetName;

	// 检查资产冲突
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FString TestPath = FullPath + TEXT(".") + AssetName;
	if (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
	{
		int32 Suffix = 1;
		FString NewName;
		constexpr int32 MaxAttempts = 1000;

		do {
			NewName = FString::Printf(TEXT("%s_%d"), *WidgetData.WidgetName, Suffix++);
			TestPath = PackagePath / NewName + TEXT(".") + NewName;
		} while (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid() && Suffix < MaxAttempts);

		if (Suffix >= MaxAttempts)
		{
			UE_LOG(LogPenToUMG, Error, TEXT("Failed to find unique name for asset '%s'"), *WidgetData.WidgetName);
			return nullptr;
		}

		AssetName = NewName;
		FullPath = PackagePath / AssetName;
		UE_LOG(LogPenToUMG, Warning, TEXT("Asset renamed to %s"), *AssetName);
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("Failed to create package at %s"), *FullPath);
		return nullptr;
	}

	UWidgetBlueprint* WBP = CastChecked<UWidgetBlueprint>(
		FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), Package, FName(*AssetName),
			BPTYPE_Normal, UWidgetBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(), NAME_None
		)
	);

	if (!WBP || !WBP->WidgetTree)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("Failed to create Widget Blueprint"));
		return nullptr;
	}

	// v2.2: 应用Fixer修正Bounds（创建可修改副本）
	FUMGWidgetNode FixedRoot = WidgetData.RootWidget;
	FIntPoint DesignSize = UPenToUMGSettings::Get()->GetDesignSize();
	FUMGWidgetUtils::ApplyFixer(FixedRoot, DesignSize);

	// 使用统一的 Widget 创建函数
	UWidget* Root = FUMGWidgetUtils::CreateWidgetRecursive(WBP->WidgetTree, FixedRoot);
	if (!Root)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("Failed to create root widget for '%s'"), *WidgetData.WidgetName);
		return nullptr;
	}
	WBP->WidgetTree->RootWidget = Root;

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(WBP);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString FileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, WBP, *FileName, SaveArgs);

	UE_LOG(LogPenToUMG, Log, TEXT("Created Widget Blueprint at %s"), *FullPath);
	return WBP;
}
