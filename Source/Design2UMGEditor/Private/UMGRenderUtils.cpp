// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "Design2UMGLog.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Input/HittestGrid.h"
#include "Misc/ConfigCacheIni.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "WidgetBlueprintEditorUtils.h"
#include "RenderingThread.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SVirtualWindow.h"

// ============================================================================
// Widget 渲染
// ============================================================================

namespace
{
	void DumpTextBlockFontState(const TCHAR* Stage, UWidgetTree* WidgetTree, const FString& OwnerName)
	{
		if (!WidgetTree)
		{
			return;
		}

		WidgetTree->ForEachWidget([Stage, &OwnerName](UWidget* Widget)
		{
			UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
			if (!TextBlock)
			{
				return;
			}

			const FSlateFontInfo FontInfo = TextBlock->GetFont();
			const UObject* FontObject = FontInfo.FontObject;
			UE_LOG(LogDesign2UMG, Log,
				TEXT("TextFont[%s]: owner='%s' widget='%s' text='%s' fontObject='%s' typeface='%s' size=%.1f tracking=%d wrap=%.1f autoWrap=%d"),
				Stage,
				*OwnerName,
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

	const TCHAR* BrushDrawTypeToString(ESlateBrushDrawType::Type DrawType)
	{
		switch (DrawType)
		{
		case ESlateBrushDrawType::NoDrawType:
			return TEXT("NoDrawType");
		case ESlateBrushDrawType::Box:
			return TEXT("Box");
		case ESlateBrushDrawType::Border:
			return TEXT("Border");
		case ESlateBrushDrawType::Image:
			return TEXT("Image");
		case ESlateBrushDrawType::RoundedBox:
			return TEXT("RoundedBox");
		default:
			return TEXT("Unknown");
		}
	}

	void DumpWidgetVisualState(const TCHAR* Stage, UWidgetTree* WidgetTree, const FString& OwnerName)
	{
		if (!UE_LOG_ACTIVE(LogDesign2UMG, Verbose) || !WidgetTree)
		{
			return;
		}

		WidgetTree->ForEachWidget([Stage, &OwnerName](UWidget* Widget)
		{
			if (!Widget)
			{
				return;
			}

			const FGeometry& Geometry = Widget->GetCachedGeometry();
			const FVector2D LocalSize = Geometry.GetLocalSize();
			const FVector2D AbsolutePosition = Geometry.GetAbsolutePosition();
			const FVector2D DesiredSize = Widget->GetDesiredSize();

			if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				UE_LOG(LogDesign2UMG, Verbose,
					TEXT("WidgetState[%s][Text]: owner='%s' widget='%s' pos=(%.1f,%.1f) local=(%.1f,%.1f) desired=(%.1f,%.1f) text='%s'"),
					Stage,
					*OwnerName,
					*TextBlock->GetName(),
					AbsolutePosition.X,
					AbsolutePosition.Y,
					LocalSize.X,
					LocalSize.Y,
					DesiredSize.X,
					DesiredSize.Y,
					*TextBlock->GetText().ToString());
				return;
			}

			const FSlateBrush* Brush = nullptr;
			const TCHAR* Kind = nullptr;
			if (const UImage* Image = Cast<UImage>(Widget))
			{
				Brush = &Image->GetBrush();
				Kind = TEXT("Image");
			}
			else if (const UBorder* Border = Cast<UBorder>(Widget))
			{
				Brush = &Border->Background;
				Kind = TEXT("Border");
			}

			if (!Brush)
			{
				return;
			}

			const UObject* ResourceObject = Brush->GetResourceObject();
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("WidgetState[%s][%s]: owner='%s' widget='%s' pos=(%.1f,%.1f) local=(%.1f,%.1f) desired=(%.1f,%.1f) drawAs=%s image=(%.1f,%.1f) resource='%s' tint=(%.3f,%.3f,%.3f,%.3f)"),
				Stage,
				Kind,
				*OwnerName,
				*Widget->GetName(),
				AbsolutePosition.X,
				AbsolutePosition.Y,
				LocalSize.X,
				LocalSize.Y,
				DesiredSize.X,
				DesiredSize.Y,
				BrushDrawTypeToString(Brush->GetDrawType()),
				Brush->ImageSize.X,
				Brush->ImageSize.Y,
				ResourceObject ? *ResourceObject->GetPathName() : TEXT("<null>"),
				Brush->TintColor.GetSpecifiedColor().R,
				Brush->TintColor.GetSpecifiedColor().G,
				Brush->TintColor.GetSpecifiedColor().B,
				Brush->TintColor.GetSpecifiedColor().A);
		});
	}

	void WarmUpTextureResource(UTexture2D* Texture)
	{
		if (!Texture)
		{
			return;
		}

		Texture->ConditionalPostLoad();
		Texture->FinishCachePlatformData();
		Texture->TemporarilyDisableStreaming();
		Texture->UpdateResource();
		Texture->SetForceMipLevelsToBeResident(30.0f);
		Texture->WaitForStreaming();
		Texture->GetPlatformData();
	}

	void WarmUpWidgetResources(UWidgetTree* WidgetTree)
	{
		if (!WidgetTree)
		{
			return;
		}

		WidgetTree->ForEachWidget([](UWidget* Widget)
		{
			if (!Widget)
			{
				return;
			}

			if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
			{
				if (const UObject* FontObject = TextBlock->GetFont().FontObject.Get())
				{
					const_cast<UObject*>(FontObject)->ConditionalPostLoad();
				}
			}

			const FSlateBrush* Brush = nullptr;
			if (const UImage* Image = Cast<UImage>(Widget))
			{
				Brush = &Image->GetBrush();
			}
			else if (const UBorder* Border = Cast<UBorder>(Widget))
			{
				Brush = &Border->Background;
			}

			if (Brush)
			{
				if (UTexture2D* Texture = Cast<UTexture2D>(Brush->GetResourceObject()))
				{
					WarmUpTextureResource(Texture);
				}
			}
		});
	}

	FVector2D GetBlueprintDesignSize(UWidgetBlueprint* WidgetBP)
	{
		if (!WidgetBP || !WidgetBP->GeneratedClass)
		{
			return FVector2D::ZeroVector;
		}

		if (const UUserWidget* CDO = Cast<UUserWidget>(WidgetBP->GeneratedClass->GetDefaultObject()))
		{
			if (CDO->DesignTimeSize.X > 1.0f && CDO->DesignTimeSize.Y > 1.0f)
			{
				return CDO->DesignTimeSize;
			}
		}

		return FVector2D::ZeroVector;
	}

	bool ShouldScaleToRenderTarget(const FVector2D& DesignSize, const FIntPoint& RenderSize)
	{
		if (DesignSize.X <= 1.0f || DesignSize.Y <= 1.0f || RenderSize.X <= 1 || RenderSize.Y <= 1)
		{
			return false;
		}

		const double DesignAspect = DesignSize.X / DesignSize.Y;
		const double RenderAspect = static_cast<double>(RenderSize.X) / static_cast<double>(RenderSize.Y);
		const double RelativeAspectDelta = FMath::Abs(DesignAspect - RenderAspect) / FMath::Max(DesignAspect, RenderAspect);
		return RelativeAspectDelta <= 0.08;
	}

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
	WarmUpWidgetResources(Widget->WidgetTree);
	if (!GUsingNullRHI)
	{
		FlushRenderingCommands();
	}
	DumpTextBlockFontState(TEXT("compiled-instance"), Widget->WidgetTree, WidgetBP->GetPathName());
	Widget->ForceLayoutPrepass();
	DumpWidgetVisualState(TEXT("compiled-instance"), Widget->WidgetTree, WidgetBP->GetPathName());
	Widget->SetFlags(RF_Transient);
	Widget->AddToRoot();

	TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	TSharedRef<SWidget> WindowContent = SlateWidget;
	const FVector2D DesignSize = GetBlueprintDesignSize(WidgetBP);
	if (ShouldScaleToRenderTarget(DesignSize, RenderSize)
		&& (!FMath::IsNearlyEqual(DesignSize.X, RenderSize.X)
			|| !FMath::IsNearlyEqual(DesignSize.Y, RenderSize.Y)))
	{
		WindowContent =
			SNew(SScaleBox)
			.Stretch(EStretch::ScaleToFit)
			.StretchDirection(EStretchDirection::Both)
			[
				SNew(SBox)
				.WidthOverride(DesignSize.X)
				.HeightOverride(DesignSize.Y)
				[
					SlateWidget
				]
			];
		UE_LOG(LogDesign2UMG, Log,
			TEXT("RenderWidgetToPng: scaling widget from design %.0fx%.0f into target %dx%d"),
			DesignSize.X, DesignSize.Y, RenderSize.X, RenderSize.Y);
	}
	else if (DesignSize.X > 1.0f && DesignSize.Y > 1.0f)
	{
		const double DesignAspect = DesignSize.X / DesignSize.Y;
		const double RenderAspect = static_cast<double>(RenderSize.X) / static_cast<double>(RenderSize.Y);
		UE_LOG(LogDesign2UMG, Log,
			TEXT("RenderWidgetToPng: skip scaling due to aspect mismatch design=%.4f target=%.4f (design %.0fx%.0f target %dx%d)"),
			DesignAspect, RenderAspect, DesignSize.X, DesignSize.Y, RenderSize.X, RenderSize.Y);
	}
	TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
	Window->SetContent(WindowContent);
	const FIntPoint EffectiveSize = ComputePreviewRenderSize(Widget, RenderSize, Window);
	Window->Resize(FVector2D(EffectiveSize.X, EffectiveSize.Y));
	Window->SlatePrepass(1.0f);
	Widget->ForceLayoutPrepass();
	DumpWidgetVisualState(TEXT("compiled-after-window-prepass"), Widget->WidgetTree, WidgetBP->GetPathName());

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
	Widget->ForceLayoutPrepass();
	DumpWidgetVisualState(TEXT("compiled-after-draw"), Widget->WidgetTree, WidgetBP->GetPathName());

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


