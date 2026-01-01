// Copyright Epic Games, Inc. All Rights Reserved.
// 测试UMG样例提取功能的控制台命令

#pragma once

#include "CoreMinimal.h"
#include "UMGSampleExtractor.h"

/**
 * 注册测试命令（在模块启动时调用）
 */
class FUMGExtractorTest
{
public:
	static void Register()
	{
		// 注册控制台命令
		IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("ImageToUMG.TestExtract"),
			TEXT("测试从Widget Blueprint提取Slot数据"),
			FConsoleCommandWithArgsDelegate::CreateStatic(&TestExtract),
			ECVF_Default
		);
	}

	static void TestExtract(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("用法: ImageToUMG.TestExtract <WidgetBlueprintPath>"));
			UE_LOG(LogTemp, Warning, TEXT("示例: ImageToUMG.TestExtract /Game/UI/WBP_Test"));
			return;
		}

		FString Path = Args[0];
		TArray<FUMGExtractedSample> Samples;

		int32 Count = FUMGSampleExtractor::ExtractFromWidgetBlueprint(Path, Samples);

		UE_LOG(LogTemp, Log, TEXT("========== 提取结果 =========="));
		UE_LOG(LogTemp, Log, TEXT("路径: %s"), *Path);
		UE_LOG(LogTemp, Log, TEXT("提取数量: %d"), Count);

		for (int32 i = 0; i < Samples.Num(); ++i)
		{
			const FUMGExtractedSample& S = Samples[i];
			UE_LOG(LogTemp, Log, TEXT("[%d] %s (%s)"), i, *S.WidgetName, *S.WidgetType);
			UE_LOG(LogTemp, Log, TEXT("    Bounds: X=%.4f Y=%.4f W=%.4f H=%.4f"),
				S.Bounds.X, S.Bounds.Y, S.Bounds.W, S.Bounds.H);
			UE_LOG(LogTemp, Log, TEXT("    Anchors: (%.2f,%.2f)-(%.2f,%.2f)"),
				S.OriginalSlot.Anchors.Min.X, S.OriginalSlot.Anchors.Min.Y,
				S.OriginalSlot.Anchors.Max.X, S.OriginalSlot.Anchors.Max.Y);
		}

		// 导出到JSON
		FString OutputPath = FPaths::ProjectSavedDir() / TEXT("ImageToUMG/TestSamples.json");
		if (FUMGSampleExtractor::ExportToJson(Samples, OutputPath))
		{
			UE_LOG(LogTemp, Log, TEXT("已导出到: %s"), *OutputPath);
		}
	}
};
