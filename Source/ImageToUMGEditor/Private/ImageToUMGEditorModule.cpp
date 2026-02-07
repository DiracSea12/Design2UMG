// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImageToUMGEditorModule.h"
#include "ImageToUMGLog.h"
#include "UMGGenerator.h"
#include "UMGJsonParser.h"
#include "UMGWidgetUtils.h"
#include "PenMCPBridge.h"

#include "WidgetBlueprint.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY(LogImageToUMG);

#define LOCTEXT_NAMESPACE "FImageToUMGEditorModule"

void FImageToUMGEditorModule::StartupModule()
{
	// PenMCPBridge 控制台入口
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ImageToUMG.PenConvert"),
		TEXT("从MCP JSON文件转换: ImageToUMG.PenConvert <nodes.json> <vars.json> [TargetNodeId]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogImageToUMG, Warning, TEXT("用法: ImageToUMG.PenConvert <nodes.json> <vars.json> [TargetNodeId]"));
				return;
			}
			FString NodesStr, VarsStr;
			if (!FFileHelper::LoadFileToString(NodesStr, *Args[0]))
			{
				UE_LOG(LogImageToUMG, Error, TEXT("无法读取: %s"), *Args[0]);
				return;
			}
			if (!FFileHelper::LoadFileToString(VarsStr, *Args[1]))
			{
				UE_LOG(LogImageToUMG, Error, TEXT("无法读取: %s"), *Args[1]);
				return;
			}
			FString TargetId = Args.Num() > 2 ? Args[2] : TEXT("");
			FUMGWidgetData Data;
			if (FPenMCPBridge::ConvertFromMCP(NodesStr, VarsStr, Data, TargetId))
			{
				UE_LOG(LogImageToUMG, Log, TEXT("[OK] PenConvert 成功, Root children: %d"), Data.RootWidget.Children.Num());
				for (int32 i = 0; i < Data.RootWidget.Children.Num(); ++i)
				{
					const auto& C = Data.RootWidget.Children[i];
					UE_LOG(LogImageToUMG, Log, TEXT("  [%d] %s (%s)"), i, *C.Name, *C.Type);
				}

				FString PkgPath = TEXT("/Game/ImageToUMG/PenConvert_Output");
				UWidgetBlueprint* WBP = FUMGGenerator::GenerateWidgetBlueprint(Data, PkgPath);
				if (WBP)
				{
					UE_LOG(LogImageToUMG, Log, TEXT("[OK] Widget Blueprint 已生成: %s"), *WBP->GetPathName());

					TArray<uint8> PngData;
					FIntPoint Size;
					if (FUMGWidgetUtils::RenderWidgetToPng(WBP, FIntPoint(400, 300), PngData, Size))
					{
						FString OutPng = FPaths::ProjectSavedDir() / TEXT("ImageToUMG/rendered.png");
						FFileHelper::SaveArrayToFile(PngData, *OutPng);
						UE_LOG(LogImageToUMG, Log, TEXT("[OK] 渲染截图已保存: %s (%dx%d)"), *OutPng, Size.X, Size.Y);
					}
					else
					{
						UE_LOG(LogImageToUMG, Error, TEXT("[FAIL] 渲染截图失败"));
					}
				}
				else
				{
					UE_LOG(LogImageToUMG, Error, TEXT("[FAIL] Widget Blueprint 生成失败"));
				}
			}
			else
			{
				UE_LOG(LogImageToUMG, Error, TEXT("[FAIL] PenConvert 失败"));
			}
		}),
		ECVF_Default
	);
}

void FImageToUMGEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FImageToUMGEditorModule, ImageToUMGEditor)
