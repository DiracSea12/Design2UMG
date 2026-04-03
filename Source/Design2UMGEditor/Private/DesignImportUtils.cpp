// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportUtils.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "Automation/Design2UMGAutomationRender.h"
#include "UMGGenerator.h"
#include "UMGWidgetUtils.h"
#include "Design2UMGSettings.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "RenderingThread.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DesignImportUtils"

namespace
{
	const TCHAR* ImportUtilsFallbackOutputPath = TEXT("/Game/Design2UMG/PenConvert_Output");

	FString ResolveImportOutputPath(const FString& OutputPath)
	{
		return OutputPath.IsEmpty() ? FString(ImportUtilsFallbackOutputPath) : OutputPath;
	}

	FIntPoint ResolveCompiledRenderSize(UWidgetBlueprint* WidgetBlueprint, const FIntPoint& RequestedRenderSize)
	{
		if (RequestedRenderSize.X > 1 && RequestedRenderSize.Y > 1)
		{
			return RequestedRenderSize;
		}

		if (WidgetBlueprint && WidgetBlueprint->GeneratedClass)
		{
			if (const UUserWidget* CDO = Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject()))
			{
				const FVector2D DesignTimeSize = CDO->DesignTimeSize;
				if (DesignTimeSize.X > 1.0f && DesignTimeSize.Y > 1.0f)
				{
					return FIntPoint(
						FMath::RoundToInt(DesignTimeSize.X),
						FMath::RoundToInt(DesignTimeSize.Y));
				}
			}
		}

		const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		return Settings ? Settings->GetDesignSize() : FIntPoint(1920, 1080);
	}

	FIntPoint ResolveWidgetTreeRenderSize(const FUMGWidgetData& Data)
	{
		int32 RenderW = FMath::CeilToInt(Data.RootWidget.Bounds.W);
		int32 RenderH = FMath::CeilToInt(Data.RootWidget.Bounds.H);
		if (RenderW < 2 || RenderH < 2)
		{
			RenderW = 1920;
			RenderH = 1080;
		}

		return FIntPoint(RenderW, RenderH);
	}

	bool SavePngBytesToFile(const TArray<uint8>& PngData, const FString& OutputPngFilePath)
	{
		if (OutputPngFilePath.IsEmpty())
		{
			return false;
		}

		const FString OutputDir = FPaths::GetPath(OutputPngFilePath);
		if (!OutputDir.IsEmpty())
		{
			IFileManager::Get().MakeDirectory(*OutputDir, true);
		}

		return FFileHelper::SaveArrayToFile(PngData, *OutputPngFilePath);
	}

	bool RenderWidgetTreeToPngFile(
		UWidgetBlueprint* WidgetBlueprint,
		const FIntPoint& RequestedRenderSize,
		const FString& OutputPngFilePath,
		FIntPoint* OutRenderedSize = nullptr)
	{
		if (!WidgetBlueprint)
		{
			return false;
		}

		const FIntPoint RenderSize = ResolveCompiledRenderSize(WidgetBlueprint, RequestedRenderSize);
		TArray<uint8> PngData;
		FIntPoint ActualSize = RenderSize;
		if (!FUMGWidgetUtils::RenderWidgetTreeToPng(WidgetBlueprint, RenderSize, PngData, ActualSize))
		{
			return false;
		}

		if (!SavePngBytesToFile(PngData, OutputPngFilePath))
		{
			return false;
		}

		if (OutRenderedSize)
		{
			*OutRenderedSize = ActualSize;
		}

		UE_LOG(LogDesign2UMG, Log,
			TEXT("DesignImportUtils: WidgetTree PNG saved: %s (%dx%d)"),
			*OutputPngFilePath, ActualSize.X, ActualSize.Y);
		return true;
	}

	UWidgetBlueprint* ReloadWidgetBlueprintForRender(UWidgetBlueprint* WidgetBlueprint)
	{
		if (!WidgetBlueprint)
		{
			return nullptr;
		}

		const FString ObjectPath = WidgetBlueprint->GetPathName();
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		if (PackageName.IsEmpty())
		{
			return WidgetBlueprint;
		}

		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			if (!Package)
			{
				return WidgetBlueprint;
			}
		}

		FlushAsyncLoading();
		ResetLoaders(static_cast<UObject*>(Package));
		if (!ReloadPackage(Package, LOAD_None))
		{
			return WidgetBlueprint;
		}

		FlushRenderingCommands();
		CollectGarbage(RF_NoFlags, true);
		if (UWidgetBlueprint* ReloadedBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath))
		{
			return ReloadedBlueprint;
		}

		return WidgetBlueprint;
	}

	int32 CountWidgets(const FUMGWidgetNode& Node)
	{
		int32 Count = 1;
		for (const FUMGWidgetNode& Child : Node.Children)
		{
			Count += CountWidgets(Child);
		}
		return Count;
	}

	void NotifyImportSummary(const FUMGWidgetData& Data, const double ElapsedSeconds)
	{
		if (Data.Warnings.Num() > 0)
		{
			FDesign2UMGNotification::Warning(FText::Format(
				LOCTEXT("ImportSummaryWarnings", "Design2UMG: {0} warnings"),
				FText::AsNumber(Data.Warnings.Num())));
			return;
		}

		FDesign2UMGNotification::Success(FText::Format(
			LOCTEXT("ImportSummaryOk", "Design2UMG: {0} widgets ({1}s)"),
			FText::AsNumber(CountWidgets(Data.RootWidget)),
			FText::FromString(FString::Printf(TEXT("%.1f"), ElapsedSeconds))));
	}
}

// Blueprint 生成

void DesignImportUtils::ApplyPendingIconFontMappingChanges(const FUMGWidgetData& Data)
{
	if (!Data.IconFontMappingChanges.HasChanges())
	{
		return;
	}

	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	if (!Settings)
	{
		return;
	}

	bool bModified = false;
	for (const FString& Family : Data.IconFontMappingChanges.Removals)
	{
		if (Family.IsEmpty())
		{
			continue;
		}

		if (Settings->IconFontAssetMap.Remove(Family) > 0)
		{
			bModified = true;
			UE_LOG(LogDesign2UMG, Display, TEXT("Removed stale icon font mapping for '%s'"), *Family);
		}
	}

	for (const TPair<FString, FString>& Pair : Data.IconFontMappingChanges.Upserts)
	{
		if (Pair.Key.IsEmpty() || Pair.Value.IsEmpty())
		{
			continue;
		}

		const TSoftObjectPtr<UFont>* Existing = Settings->IconFontAssetMap.Find(Pair.Key);
		if (Existing && Existing->ToString() == Pair.Value)
		{
			continue;
		}

		Settings->IconFontAssetMap.Add(Pair.Key, TSoftObjectPtr<UFont>(FSoftObjectPath(Pair.Value)));
		bModified = true;
		UE_LOG(LogDesign2UMG, Display, TEXT("Persisted icon font mapping '%s' -> '%s'"), *Pair.Key, *Pair.Value);
	}

	if (bModified)
	{
		if (Design2UMGAutomationRender::IsAutomationRenderActive())
		{
			UE_LOG(LogDesign2UMG, Display, TEXT("Skip persisting icon font mapping changes during automation render"));
		}
		else
		{
			Settings->SaveConfig();
		}
	}
}

UWidgetBlueprint* DesignImportUtils::GenerateBlueprint(const FUMGWidgetData& Data, const FString& OutputPath)
{
	ApplyPendingIconFontMappingChanges(Data);
	const double StartTime = FPlatformTime::Seconds();
	const FString FinalPath = ResolveImportOutputPath(OutputPath);

	UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, FinalPath);
	if (!WBP)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint generation failed"));
		FDesign2UMGNotification::Error(LOCTEXT("GenFailed", "Design2UMG: Widget Blueprint generation failed"));
		return nullptr;
	}
	UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Generated %s"), *WBP->GetPathName());

	NotifyImportSummary(Data, FPlatformTime::Seconds() - StartTime);
	return WBP;
}

bool DesignImportUtils::GenerateBlueprintAndPng(
	const FUMGWidgetData& Data,
	const FString& OutputPath,
	FString* OutGeneratedAssetPath,
	FString* OutRenderPngPath)
{
	ApplyPendingIconFontMappingChanges(Data);
	const FString FinalPath = ResolveImportOutputPath(OutputPath);

	// 先构建（不立即落盘），再用瞬态实例渲染 PNG，最后统一保存 Blueprint。
	UWidgetBlueprint* WBP = FUMGGenerator::BuildWidgetBlueprint(Data, FinalPath);
	if (!WBP)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget Blueprint build failed"));
		return false;
	}
	if (OutGeneratedAssetPath)
	{
		*OutGeneratedAssetPath = WBP->GetPathName();
	}

	// 渲染尺寸优先使用根节点 Bounds；合成根（如多 frame 的 CanvasPanel）Bounds 为 0，
	// 退化到 1x1 会导致 PNG 无意义，此时使用 1920x1080 默认尺寸。
	const FIntPoint FallbackSize = ResolveWidgetTreeRenderSize(Data);
	FIntPoint Size = FallbackSize;
	const FString PngName = WBP ? WBP->GetName() : FPaths::GetBaseFilename(FinalPath);
	const FString OutPng = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (PngName + TEXT(".png"));
	if (OutRenderPngPath)
	{
		*OutRenderPngPath = OutPng;
	}
	if (!RenderWidgetTreeToPngFile(WBP, FallbackSize, OutPng, &Size))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Widget PNG render failed"));
		// 渲染失败时也保存 Blueprint，方便定位问题。
		FUMGGenerator::SaveWidgetBlueprint(WBP);
		return false;
	}

	// 成功渲染后保存 Blueprint
	FUMGGenerator::SaveWidgetBlueprint(WBP);
	if (OutGeneratedAssetPath)
	{
		*OutGeneratedAssetPath = WBP->GetPathName();
	}
	if (OutRenderPngPath)
	{
		*OutRenderPngPath = OutPng;
	}
	UE_LOG(LogDesign2UMG, Log, TEXT("DesignImportUtils: Rendered PNG saved: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
	return true;
}

bool DesignImportUtils::RenderCompiledBlueprintToPngFile(
	UWidgetBlueprint* WidgetBlueprint,
	const FString& OutputPngFilePath,
	const FIntPoint& RequestedRenderSize,
	FIntPoint* OutRenderedSize)
{
	if (!WidgetBlueprint)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: RenderCompiledBlueprintToPngFile invalid WidgetBlueprint"));
		return false;
	}

	const FIntPoint RenderSize = ResolveCompiledRenderSize(WidgetBlueprint, RequestedRenderSize);
	TArray<uint8> PngData;
	FIntPoint ActualSize = RenderSize;
	if (!FUMGWidgetUtils::RenderWidgetToPng(WidgetBlueprint, RenderSize, PngData, ActualSize))
	{
		UE_LOG(LogDesign2UMG, Error,
			TEXT("DesignImportUtils: RenderCompiledBlueprintToPngFile failed for %s"),
			*WidgetBlueprint->GetPathName());
		return false;
	}

	if (!SavePngBytesToFile(PngData, OutputPngFilePath))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("DesignImportUtils: Failed to save compiled PNG: %s"), *OutputPngFilePath);
		return false;
	}

	if (OutRenderedSize)
	{
		*OutRenderedSize = ActualSize;
	}

	UE_LOG(LogDesign2UMG, Log,
		TEXT("DesignImportUtils: Compiled PNG saved: %s (%dx%d)"),
		*OutputPngFilePath, ActualSize.X, ActualSize.Y);
	return true;
}

bool DesignImportUtils::GenerateBlueprintAndCompiledPng(
	const FUMGWidgetData& Data,
	const FString& OutputPath,
	const FString& OutputPngFilePath,
	const FIntPoint& RequestedRenderSize,
	FString* OutGeneratedAssetPath,
	FIntPoint* OutRenderedSize)
{
	ApplyPendingIconFontMappingChanges(Data);
	UWidgetBlueprint* WidgetBlueprint = GenerateBlueprint(Data, OutputPath);
	if (!WidgetBlueprint)
	{
		return false;
	}

	if (OutGeneratedAssetPath)
	{
		*OutGeneratedAssetPath = WidgetBlueprint->GetPathName();
	}

	{
		const FString TreeRenderPath = FPaths::Combine(
			FPaths::GetPath(OutputPngFilePath),
			FPaths::GetBaseFilename(OutputPngFilePath) + TEXT(".widgettree.png"));
		RenderWidgetTreeToPngFile(WidgetBlueprint, RequestedRenderSize, TreeRenderPath);
	}

	WidgetBlueprint = ReloadWidgetBlueprintForRender(WidgetBlueprint);

	return RenderCompiledBlueprintToPngFile(
		WidgetBlueprint,
		OutputPngFilePath,
		RequestedRenderSize,
		OutRenderedSize);
}

#undef LOCTEXT_NAMESPACE





