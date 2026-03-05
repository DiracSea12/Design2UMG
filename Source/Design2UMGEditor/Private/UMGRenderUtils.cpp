// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "Design2UMGLog.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "TextureResource.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Input/HittestGrid.h"
#include "Misc/ConfigCacheIni.h"
#include "Slate/WidgetRenderer.h"
#include "WidgetBlueprintEditorUtils.h"
#include "RenderingThread.h"
#include "Widgets/SVirtualWindow.h"

// ============================================================================
// Widget 渲染
// ============================================================================

namespace
{
	FIntPoint GetDesignerPreviewWindowSize(const FIntPoint& FallbackSize)
	{
		int32 PreviewWidth = 0;
		int32 PreviewHeight = 0;
		if (GConfig)
		{
			GConfig->GetInt(TEXT("UMGEditor.Designer"), TEXT("PreviewWidth"), PreviewWidth, GEditorPerProjectIni);
			GConfig->GetInt(TEXT("UMGEditor.Designer"), TEXT("PreviewHeight"), PreviewHeight, GEditorPerProjectIni);
		}
		if (PreviewWidth > 0 && PreviewHeight > 0)
		{
			return FIntPoint(PreviewWidth, PreviewHeight);
		}
		return FallbackSize;
	}

	FIntPoint ComputePreviewRenderSize(UUserWidget* Widget, const FIntPoint& FallbackSize, const TSharedRef<SVirtualWindow>& Window)
	{
		if (!Widget) return FallbackSize;

		// 直接用调用方传入的尺寸，不读编辑器 Designer 配置（那是给 UMG 预览用的）
		Window->Resize(FVector2D(FallbackSize.X, FallbackSize.Y));
		Window->SlatePrepass(1.0f);
		Window->SlatePrepass(1.0f);
		return FallbackSize;
	}
}

bool FUMGWidgetUtils::RenderWidgetToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize)
{
	if (!WidgetBP || !WidgetBP->GeneratedClass)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("RenderWidgetToPng: invalid WidgetBP"));
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return false;

	UUserWidget* Widget = CreateWidget<UUserWidget>(World, TSubclassOf<UUserWidget>(WidgetBP->GeneratedClass));
	if (!Widget) return false;

	Widget->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
	Widget->PreConstruct(true);
	Widget->SynchronizeProperties();
	Widget->ForceLayoutPrepass();
	Widget->SetFlags(RF_Transient);
	Widget->AddToRoot();

	TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
	Window->SetContent(SlateWidget);
	const FIntPoint EffectiveSize = ComputePreviewRenderSize(Widget, RenderSize, Window);

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
	RT->Filter = TF_Bilinear;
	RT->ClearColor = FLinearColor::Transparent;
	RT->SRGB = true;
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->InitCustomFormat(EffectiveSize.X, EffectiveSize.Y, PF_B8G8R8A8, true);

	FWidgetRenderer Renderer(true);
	Renderer.SetIsPrepassNeeded(false);
	TUniquePtr<FHittestGrid> HitTestGrid = MakeUnique<FHittestGrid>();
	FProceduralShapeGenerator::FlushPendingTextureUpdates();
	FDesignPathRasterizer::FlushPendingTextureUpdates();
	Renderer.DrawWindow(RT, *HitTestGrid, Window, 1.0f, FVector2D(EffectiveSize.X, EffectiveSize.Y), 0.1f);
	if (!GUsingNullRHI) { FlushRenderingCommands(); }

	FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		Widget->RemoveFromRoot();
		RT->ReleaseResource();
		RT->MarkAsGarbage();
		return false;
	}

	TArray<FColor> Pixels;
	Resource->ReadPixels(Pixels);
	RT->ReleaseResource();
	RT->MarkAsGarbage();
	Widget->RemoveFromRoot();

	if (Pixels.Num() == 0) return false;

	IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid()) return false;

	Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), EffectiveSize.X, EffectiveSize.Y, ERGBFormat::BGRA, 8);
	OutPngData = Wrapper->GetCompressed();
	OutSize = EffectiveSize;
	return OutPngData.Num() > 0;
}

// ============================================================================
// 直接从 WidgetTree 渲染（编译前，transient 纹理仍有效）
// ============================================================================

namespace
{
	bool RenderSlateWidgetToPngData(TSharedRef<SWidget> SlateWidget, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize)
	{
		TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
		Window->SetContent(SlateWidget);
		Window->Resize(FVector2D(RenderSize.X, RenderSize.Y));
		Window->SlatePrepass(1.0f);

		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
		RT->Filter = TF_Bilinear;
		RT->ClearColor = FLinearColor::Transparent;
		RT->SRGB = true;
		RT->RenderTargetFormat = RTF_RGBA8;
		RT->InitCustomFormat(RenderSize.X, RenderSize.Y, PF_B8G8R8A8, true);

		FWidgetRenderer Renderer(true);
		Renderer.SetIsPrepassNeeded(false);
		TUniquePtr<FHittestGrid> HitTestGrid = MakeUnique<FHittestGrid>();
		FProceduralShapeGenerator::FlushPendingTextureUpdates();
		FDesignPathRasterizer::FlushPendingTextureUpdates();
		Renderer.DrawWindow(RT, *HitTestGrid, Window, 1.0f, FVector2D(RenderSize.X, RenderSize.Y), 0.1f);
		if (!GUsingNullRHI) { FlushRenderingCommands(); }

		FTextureRenderTargetResource* Resource = RT->GameThread_GetRenderTargetResource();
		if (!Resource)
		{
			RT->ReleaseResource();
			RT->MarkAsGarbage();
			return false;
		}

		TArray<FColor> Pixels;
		Resource->ReadPixels(Pixels);
		RT->ReleaseResource();
		RT->MarkAsGarbage();

		if (Pixels.Num() == 0) return false;

		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid()) return false;

		Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), RenderSize.X, RenderSize.Y, ERGBFormat::BGRA, 8);
		OutPngData = Wrapper->GetCompressed();
		OutSize = RenderSize;
		return OutPngData.Num() > 0;
	}
}

bool FUMGWidgetUtils::RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize)
{
	if (!WidgetBP || !WidgetBP->WidgetTree || !WidgetBP->WidgetTree->RootWidget)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("RenderWidgetTreeToPng: invalid WidgetBP or empty WidgetTree"));
		return false;
	}

	if (!FSlateApplication::IsInitialized()) return false;

	// 遍历 WidgetTree 所有 Widget，确保 SWidget 被创建并同步属性
	WidgetBP->WidgetTree->ForEachWidget([](UWidget* W)
	{
		if (!W) return;
		W->TakeWidget();
		W->SynchronizeProperties();
	});

	UWidget* RootWidget = WidgetBP->WidgetTree->RootWidget;
	TSharedPtr<SWidget> SlateWidget = RootWidget->GetCachedWidget();
	if (!SlateWidget.IsValid())
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("RenderWidgetTreeToPng: failed to get SWidget from RootWidget"));
		return false;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("RenderWidgetTreeToPng: rendering directly from WidgetTree (%dx%d)"), RenderSize.X, RenderSize.Y);
	return RenderSlateWidgetToPngData(SlateWidget.ToSharedRef(), RenderSize, OutPngData, OutSize);
}


