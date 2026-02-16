// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

UWidgetBlueprint* FUMGGenerator::GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
{
	UWidgetBlueprint* WBP = BuildWidgetBlueprint(WidgetData, PackagePath);
	if (WBP)
		SaveWidgetBlueprint(WBP);
	return WBP;
}

UWidgetBlueprint* FUMGGenerator::BuildWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
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
			NewName = FString::Printf(TEXT("%s_%d"), *WidgetData.WidgetName, Suffix);
			TestPath = PackagePath / NewName + TEXT(".") + NewName;
			++Suffix;
		} while (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid() && Suffix <= MaxAttempts);

		if (Suffix > MaxAttempts && AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("Failed to find unique name for asset '%s'"), *WidgetData.WidgetName);
			return nullptr;
		}

		AssetName = NewName;
		FullPath = PackagePath / AssetName;
		UE_LOG(LogDesign2UMG, Warning, TEXT("Asset renamed to %s"), *AssetName);
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create package at %s"), *FullPath);
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
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create Widget Blueprint"));
		return nullptr;
	}

	// v2.2: 应用Fixer修正Bounds（创建可修改副本）
	FUMGWidgetNode FixedRoot = WidgetData.RootWidget;
	FIntPoint DesignSize = UDesign2UMGSettings::Get()->GetDesignSize();
	// 设计稿实际尺寸可能超过配置的 DesignSize（如设计系统组件库 4865x2781）
	// Fixer 归一化/反归一化必须使用实际尺寸，否则坐标会被压缩
	if (FixedRoot.Bounds.W > DesignSize.X || FixedRoot.Bounds.H > DesignSize.Y)
	{
		DesignSize.X = FMath::Max(DesignSize.X, FMath::CeilToInt(FixedRoot.Bounds.W));
		DesignSize.Y = FMath::Max(DesignSize.Y, FMath::CeilToInt(FixedRoot.Bounds.H));
		UE_LOG(LogDesign2UMG, Log, TEXT("DesignSize overridden to %dx%d (root bounds %.0fx%.0f)"),
			DesignSize.X, DesignSize.Y, FixedRoot.Bounds.W, FixedRoot.Bounds.H);
	}
	FUMGWidgetUtils::EffectiveDesignSize = DesignSize;
	FUMGWidgetUtils::ApplyFixer(FixedRoot, DesignSize);

	// Dump widget tree for debugging
	{
		TFunction<void(const FUMGWidgetNode&, int32)> DumpNode = [&](const FUMGWidgetNode& Node, int32 Depth)
		{
			FString Indent;
			for (int32 i = 0; i < Depth; ++i) Indent += TEXT("  ");

			FString SlotInfo = FString::Printf(TEXT("slot[h=%s v=%s sz=%s fw=%.1f]"),
				*Node.Slot.HorizontalAlignment, *Node.Slot.VerticalAlignment,
				*Node.Slot.SizeRule, Node.Slot.FillWeight);

			FString PropsInfo;
			for (const auto& P : Node.Properties)
			{
				if (P.Key == TEXT("PathGeometry") || P.Key == TEXT("MaterialParams")) continue;
				PropsInfo += FString::Printf(TEXT(" %s=%s"), *P.Key, *P.Value.Left(40));
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("TREE: %s[%s] '%s' bounds=(%.0f,%.0f,%.0f,%.0f) %s%s"),
				*Indent, *Node.Type, *Node.Name,
				Node.Bounds.X, Node.Bounds.Y, Node.Bounds.W, Node.Bounds.H,
				*SlotInfo, *PropsInfo);

			for (const auto& Child : Node.Children)
				DumpNode(Child, Depth + 1);
		};
		UE_LOG(LogDesign2UMG, Log, TEXT("=== Widget Tree Dump (after Fixer) ==="));
		DumpNode(FixedRoot, 0);
		UE_LOG(LogDesign2UMG, Log, TEXT("=== End Widget Tree Dump ==="));
	}

	// 使用统一的 Widget 创建函数
	UWidget* Root = FUMGWidgetUtils::CreateWidgetRecursive(WBP->WidgetTree, FixedRoot);
	if (!Root)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to create root widget for '%s'"), *WidgetData.WidgetName);
		return nullptr;
	}
	WBP->WidgetTree->RootWidget = Root;

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(WBP);

	UE_LOG(LogDesign2UMG, Log, TEXT("Built Widget Blueprint at %s (not yet compiled/saved)"), *FullPath);
	return WBP;
}

void FUMGGenerator::SaveWidgetBlueprint(UWidgetBlueprint* WBP)
{
	if (!WBP) return;

	// 编译（此时 transient 纹理数据可能丢失，但渲染已完成）
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
	FKismetEditorUtilities::CompileBlueprint(WBP);

	UPackage* Package = WBP->GetOutermost();
	FString FullPath = Package->GetName();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString FileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, WBP, *FileName, SaveArgs);

	UE_LOG(LogDesign2UMG, Log, TEXT("Saved Widget Blueprint at %s"), *FullPath);
}
