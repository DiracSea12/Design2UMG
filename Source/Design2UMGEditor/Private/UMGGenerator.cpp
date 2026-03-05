// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "ObjectTools.h"

#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"

#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"

namespace
{
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
					FMath::RoundToInt(CurrentSize.X),
					FMath::RoundToInt(CurrentSize.Y));
			}
		}

		return FIntPoint::ZeroValue;
	}

	FUMGSlotData MakeCanvasFillSlot()
	{
		FUMGSlotData Slot;
		Slot.Anchors.Min.X = 0.0f;
		Slot.Anchors.Min.Y = 0.0f;
		Slot.Anchors.Max.X = 1.0f;
		Slot.Anchors.Max.Y = 1.0f;
		Slot.Alignment.X = 0.0f;
		Slot.Alignment.Y = 0.0f;
		Slot.Offsets.Left = 0.0f;
		Slot.Offsets.Top = 0.0f;
		Slot.Offsets.Right = 0.0f;
		Slot.Offsets.Bottom = 0.0f;
		Slot.bAutoSize = false;
		return Slot;
	}

	FUMGWidgetNode WrapRootForViewport(
		FUMGWidgetNode&& InRoot,
		const FIntPoint& ViewportSize,
		const FIntPoint& ContentSize,
		bool bDownOnly)
	{
		FUMGWidgetNode SizeBoxNode;
		SizeBoxNode.Type = TEXT("SizeBox");
		SizeBoxNode.Id = TEXT("__d2u_root_sizebox");
		SizeBoxNode.Name = TEXT("D2U_RootSizeBox");
		SizeBoxNode.Bounds.X = 0.0f;
		SizeBoxNode.Bounds.Y = 0.0f;
		SizeBoxNode.Bounds.W = static_cast<float>(ContentSize.X);
		SizeBoxNode.Bounds.H = static_cast<float>(ContentSize.Y);
		SizeBoxNode.Properties.Add(TEXT("WidthOverride"), FString::FromInt(ContentSize.X));
		SizeBoxNode.Properties.Add(TEXT("HeightOverride"), FString::FromInt(ContentSize.Y));
		SizeBoxNode.Children.Add(MoveTemp(InRoot));

		FUMGWidgetNode ScaleBoxNode;
		ScaleBoxNode.Type = TEXT("ScaleBox");
		ScaleBoxNode.Id = TEXT("__d2u_root_scalebox");
		ScaleBoxNode.Name = TEXT("D2U_RootScaleBox");
		ScaleBoxNode.Bounds.X = 0.0f;
		ScaleBoxNode.Bounds.Y = 0.0f;
		ScaleBoxNode.Bounds.W = static_cast<float>(ViewportSize.X);
		ScaleBoxNode.Bounds.H = static_cast<float>(ViewportSize.Y);
		ScaleBoxNode.Slot = MakeCanvasFillSlot();
		ScaleBoxNode.Properties.Add(TEXT("Stretch"), TEXT("ScaleToFit"));
		ScaleBoxNode.Properties.Add(TEXT("StretchDirection"), bDownOnly ? TEXT("DownOnly") : TEXT("Both"));
		ScaleBoxNode.Children.Add(MoveTemp(SizeBoxNode));

		FUMGWidgetNode CanvasRoot;
		CanvasRoot.Type = TEXT("CanvasPanel");
		CanvasRoot.Id = TEXT("__d2u_viewport_root");
		CanvasRoot.Name = TEXT("D2U_ViewportRoot");
		CanvasRoot.Bounds.X = 0.0f;
		CanvasRoot.Bounds.Y = 0.0f;
		CanvasRoot.Bounds.W = static_cast<float>(ViewportSize.X);
		CanvasRoot.Bounds.H = static_cast<float>(ViewportSize.Y);
		CanvasRoot.Children.Add(MoveTemp(ScaleBoxNode));
		return CanvasRoot;
	}
}

UWidgetBlueprint* FUMGGenerator::GenerateWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
{
	UWidgetBlueprint* WBP = BuildWidgetBlueprint(WidgetData, PackagePath);
	if (WBP)
		SaveWidgetBlueprint(WBP);
	return WBP;
}

UWidgetBlueprint* FUMGGenerator::BuildWidgetBlueprint(const FUMGWidgetData& WidgetData, const FString& PackagePath)
{
	FString AssetName = ObjectTools::SanitizeObjectName(WidgetData.WidgetName);
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
			NewName = FString::Printf(TEXT("%s_%d"), *AssetName, Suffix);
			TestPath = PackagePath / NewName + TEXT(".") + NewName;
			++Suffix;
		} while (AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid() && Suffix <= MaxAttempts);

		if (Suffix > MaxAttempts && AR.GetAssetByObjectPath(FSoftObjectPath(TestPath)).IsValid())
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to find unique name for asset '%s'"), *WidgetData.WidgetName);
			UE_LOG(LogDesign2UMG, Error, TEXT("无法为资产 '%s' 生成唯一名称。请清理输出目录后重试。"), *WidgetData.WidgetName);
			return nullptr;
		}

		AssetName = NewName;
		FullPath = PackagePath / AssetName;
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Asset renamed to %s"), *AssetName);
		UE_LOG(LogDesign2UMG, Warning, TEXT("输出路径已存在同名资产，自动重命名为 %s"), *AssetName);
	}

	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create package at %s"), *FullPath);
		UE_LOG(LogDesign2UMG, Error, TEXT("无法创建资产包。请检查输出路径是否有效: %s"), *FullPath);
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
		UE_LOG(LogDesign2UMG, Error, TEXT("无法创建 Widget Blueprint。请检查引擎版本兼容性。"));
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}

	// v2.2: 应用Fixer修正Bounds（创建可修改副本）
	FUMGWidgetNode FixedRoot = WidgetData.RootWidget;
	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const FIntPoint ConfigDesignSize = Settings->GetDesignSize();
	const int32 RootWidth = FMath::CeilToInt(FixedRoot.Bounds.W);
	const int32 RootHeight = FMath::CeilToInt(FixedRoot.Bounds.H);
	const bool bHasValidRootSize = RootWidth >= 2 && RootHeight >= 2;
	// Fidelity-first: Fixer/LayoutCalc 使用设计稿根节点的真实像素尺寸，
	// 避免被用户配置尺寸放大/压缩而产生系统性偏移。
	FIntPoint DesignSize = bHasValidRootSize
		? FIntPoint(RootWidth, RootHeight)
		: ConfigDesignSize;
	if (bHasValidRootSize && (DesignSize != ConfigDesignSize))
	{
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Using root design size %dx%d (config %dx%d, root bounds %.0fx%.0f)"),
			DesignSize.X, DesignSize.Y,
			ConfigDesignSize.X, ConfigDesignSize.Y,
			FixedRoot.Bounds.W, FixedRoot.Bounds.H);
	}
	else if (!bHasValidRootSize)
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("Root bounds invalid (%.2fx%.2f), fallback to config design size %dx%d"),
			FixedRoot.Bounds.W, FixedRoot.Bounds.H,
			ConfigDesignSize.X, ConfigDesignSize.Y);
	}
	FUMGWidgetUtils::EffectiveDesignSize = DesignSize;
	if (Settings->bEnableLayoutFixer)
	{
		FUMGWidgetUtils::ApplyFixer(FixedRoot, DesignSize);
	}

	const bool bOversizedRoot =
		(FixedRoot.Bounds.W > static_cast<float>(ConfigDesignSize.X)) ||
		(FixedRoot.Bounds.H > static_cast<float>(ConfigDesignSize.Y));
	if (Settings->bAutoWrapOversizedRootForViewport && bOversizedRoot)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Wrapping oversized root for viewport fit: root=%.0fx%.0f config=%dx%d stretch=%s"),
			FixedRoot.Bounds.W, FixedRoot.Bounds.H,
			ConfigDesignSize.X, ConfigDesignSize.Y,
			Settings->bViewportScaleDownOnly ? TEXT("DownOnly") : TEXT("Both"));
		FixedRoot = WrapRootForViewport(
			MoveTemp(FixedRoot),
			ConfigDesignSize,
			DesignSize,
			Settings->bViewportScaleDownOnly);
	}

	// Dump widget tree only in verbose mode.
	if (UE_LOG_ACTIVE(LogDesign2UMG, Verbose))
	{
		TFunction<void(const FUMGWidgetNode&, int32)> DumpNode = [&](const FUMGWidgetNode& Node, int32 Depth)
		{
			FString Indent;
			for (int32 i = 0; i < Depth; ++i) Indent += TEXT("  ");

			FString SlotInfo = FString::Printf(TEXT("slot[h=%s v=%s sz=%s fw=%.1f off=(%.0f,%.0f,%.0f,%.0f) anc=(%.2f,%.2f,%.2f,%.2f) aln=(%.2f,%.2f) auto=%d]"),
				*Node.Slot.HorizontalAlignment, *Node.Slot.VerticalAlignment,
				*Node.Slot.SizeRule, Node.Slot.FillWeight,
				Node.Slot.Offsets.Left, Node.Slot.Offsets.Top, Node.Slot.Offsets.Right, Node.Slot.Offsets.Bottom,
				Node.Slot.Anchors.Min.X, Node.Slot.Anchors.Min.Y, Node.Slot.Anchors.Max.X, Node.Slot.Anchors.Max.Y,
				Node.Slot.Alignment.X, Node.Slot.Alignment.Y,
				Node.Slot.bAutoSize ? 1 : 0);

			FString PropsInfo;
			for (const auto& P : Node.Properties)
			{
				if (P.Key == TEXT("PathGeometry") || P.Key == TEXT("MaterialParams")) continue;
				PropsInfo += FString::Printf(TEXT(" %s=%s"), *P.Key, *P.Value.Left(40));
			}

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

	// 使用统一的 Widget 创建函数
	FUMGWidgetUtils::ClearNodeIdMap();
	UWidget* Root = FUMGWidgetUtils::CreateWidgetRecursive(WBP->WidgetTree, FixedRoot);
	if (!Root)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Failed to create root widget for '%s'"), *WidgetData.WidgetName);
		UE_LOG(LogDesign2UMG, Error, TEXT("无法创建根控件 '%s'。设计文件可能包含不支持的节点类型。"), *WidgetData.WidgetName);
		Package->ClearDirtyFlag();
		Package->SetFlags(RF_Transient);
		return nullptr;
	}
	WBP->WidgetTree->RootWidget = Root;

	// 设置编辑器显示尺寸到 CDO；保存时会在编译后再次回写，避免编译重置默认值。
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
		DesiredDesignSize = UDesign2UMGSettings::Get()->GetDesignSize();
	}

	// 编译（此时 transient 纹理数据可能丢失，但渲染已完成）
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);

	// MarkBlueprintAsStructurallyModified 可能触发中间编译填充 GUID map，
	// 清除后让 CompileBlueprint 走 IsEmpty() 分支从 widget tree 重建，避免 stale entry
	WBP->WidgetVariableNameToGuidMap.Empty();
	FKismetEditorUtilities::CompileBlueprint(WBP);
	ApplyDesignPreviewSize(WBP, DesiredDesignSize, TEXT("post-compile"));

	UPackage* Package = WBP->GetOutermost();
	FString FullPath = Package->GetName();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	FString FileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, WBP, *FileName, SaveArgs);

	UE_LOG(LogDesign2UMG, Log, TEXT("Saved Widget Blueprint at %s"), *FullPath);
}

