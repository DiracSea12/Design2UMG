// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMGWidgetUtils.h"
#include "PenToUMGLog.h"

#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Input/HittestGrid.h"
#include "Misc/ConfigCacheIni.h"
#include "Slate/WidgetRenderer.h"
#include "WidgetBlueprintEditorUtils.h"
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
		FIntPoint Result = FallbackSize;
		if (!Widget) return Result;

		const FIntPoint WindowSize = GetDesignerPreviewWindowSize(FallbackSize);
		Window->Resize(FVector2D(WindowSize.X, WindowSize.Y));
		Window->SlatePrepass(1.0f);

		FVector2D DesiredSize = Window->GetDesiredSize();
		if (DesiredSize.X <= SMALL_NUMBER || DesiredSize.Y <= SMALL_NUMBER)
			DesiredSize = FVector2D(WindowSize.X, WindowSize.Y);

		FVector2D UnscaledSize = FWidgetBlueprintEditorUtils::GetWidgetPreviewUnScaledCustomSize(
			DesiredSize, Widget, TOptional<FVector2D>(), EThumbnailPreviewSizeMode::MatchDesignerMode);
		if (UnscaledSize.X <= SMALL_NUMBER || UnscaledSize.Y <= SMALL_NUMBER)
			UnscaledSize = DesiredSize;

		Result.X = FMath::Max(1, FMath::CeilToInt(UnscaledSize.X));
		Result.Y = FMath::Max(1, FMath::CeilToInt(UnscaledSize.Y));
		return Result;
	}
}

bool FUMGWidgetUtils::RenderWidgetToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize)
{
	if (!WidgetBP || !WidgetBP->GeneratedClass)
	{
		UE_LOG(LogPenToUMG, Warning, TEXT("RenderWidgetToPng: invalid WidgetBP"));
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
	const EPixelFormat Fmt = FSlateApplication::Get().GetRenderer()->GetSlateRecommendedColorFormat();
	RT->InitCustomFormat(EffectiveSize.X, EffectiveSize.Y, Fmt, false);

	FWidgetRenderer Renderer(true);
	Renderer.SetIsPrepassNeeded(false);
	TUniquePtr<FHittestGrid> HitTestGrid = MakeUnique<FHittestGrid>();
	Renderer.DrawWindow(RT, *HitTestGrid, Window, 1.0f, FVector2D(EffectiveSize.X, EffectiveSize.Y), 0.1f);

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