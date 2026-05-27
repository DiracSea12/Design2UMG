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
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Input/HittestGrid.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "RenderingThread.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SVirtualWindow.h"

// ============================================================================
// Widget rendering
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

	void WarmUpFontData(const FFontData& FontData)
	{
		const UObject* FontFaceAsset = FontData.GetFontFaceAsset();
		if (!FontFaceAsset)
		{
			FontData.GetFontFaceData();
			return;
		}

		UObject* MutableFontFaceAsset = const_cast<UObject*>(FontFaceAsset);
		MutableFontFaceAsset->ConditionalPostLoad();
		if (UFontFace* FontFace = Cast<UFontFace>(MutableFontFaceAsset))
		{
#if WITH_EDITORONLY_DATA
			FontFace->CacheSubFaces();
#endif
			FontFace->GetFontFaceData();
		}
	}

	void WarmUpTypeface(const FTypeface& Typeface)
	{
		for (const FTypefaceEntry& Entry : Typeface.Fonts)
		{
			WarmUpFontData(Entry.Font);
		}
	}

	void WarmUpCompositeFont(const FCompositeFont& CompositeFont)
	{
		WarmUpTypeface(CompositeFont.DefaultTypeface);
		WarmUpTypeface(CompositeFont.FallbackTypeface.Typeface);
		for (const FCompositeSubFont& SubTypeface : CompositeFont.SubTypefaces)
		{
			WarmUpTypeface(SubTypeface.Typeface);
		}
	}

	void WarmUpFontResource(const FSlateFontInfo& FontInfo)
	{
		if (const UObject* FontObject = FontInfo.FontObject.Get())
		{
			UObject* MutableFontObject = const_cast<UObject*>(FontObject);
			MutableFontObject->ConditionalPostLoad();
			if (UFont* Font = Cast<UFont>(MutableFontObject))
			{
				if (const FCompositeFont* CompositeFont = Font->GetCompositeFont())
				{
					WarmUpCompositeFont(*CompositeFont);
				}
			}
		}

		if (const FCompositeFont* CompositeFont = FontInfo.GetCompositeFont())
		{
			WarmUpCompositeFont(*CompositeFont);
		}
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
				WarmUpFontResource(TextBlock->GetFont());
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

	FIntPoint ResolveDesignLayoutPixelSize(const FVector2D& DesignSize)
	{
		return FIntPoint(
			FMath::Max(1, FMath::CeilToInt(DesignSize.X)),
			FMath::Max(1, FMath::CeilToInt(DesignSize.Y)));
	}

	bool IsValidRenderSize(const FIntPoint& Size)
	{
		return Size.X > 1 && Size.Y > 1;
	}

	FIntPoint ComputePreviewRenderSize(const FIntPoint& FallbackSize, const TSharedRef<SVirtualWindow>& Window)
	{
		// Use the explicit layout size. Editor Designer preferences are preview-only.
		Window->Resize(FVector2D(FallbackSize.X, FallbackSize.Y));
		Window->SlatePrepass(1.0f);
		Window->SlatePrepass(1.0f);
		return FallbackSize;
	}

	void ClearRenderInstanceNavigation(UUserWidget* Widget)
	{
		if (!Widget)
		{
			return;
		}

		int32 ClearedCount = 0;
		auto ClearNavigation = [&ClearedCount](UWidget* CurrentWidget)
		{
			if (!CurrentWidget || !CurrentWidget->Navigation)
			{
				return;
			}

			// Rendering does not rely on navigation metadata. Some compiled
			// preview instances can materialize a corrupted instanced navigation
			// payload, so do not dereference it here; just sever the pointer.
			CurrentWidget->Navigation = nullptr;
			++ClearedCount;
		};

		ClearNavigation(Widget);
		if (Widget->WidgetTree)
		{
			Widget->WidgetTree->ForEachWidget(ClearNavigation);
		}

		if (ClearedCount > 0)
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("RenderWidgetToPng: cleared stale navigation on %d widget instances before TakeWidget"),
				ClearedCount);
		}
	}
}

bool FUMGWidgetUtils::RenderWidgetToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize)
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
	ClearRenderInstanceNavigation(Widget);
	Widget->ForceLayoutPrepass();
	DumpWidgetVisualState(TEXT("compiled-instance"), Widget->WidgetTree, WidgetBP->GetPathName());
	Widget->SetFlags(RF_Transient);
	Widget->AddToRoot();

	TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	const FVector2D DesignSize = GetBlueprintDesignSize(WidgetBP);
	const FIntPoint LayoutPixelSize = ResolveDesignLayoutPixelSize(DesignSize);
	const bool bHasValidDesignSize = DesignSize.X > 1.0f && DesignSize.Y > 1.0f && IsValidRenderSize(LayoutPixelSize);
	if (!bHasValidDesignSize)
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("RenderWidgetToPng: missing valid DesignTimeSize for %s; source design size is required"),
			*WidgetBP->GetPathName());
		Widget->RemoveFromRoot();
		return false;
	}

	const FIntPoint SlateRenderSize = LayoutPixelSize;
	if (!IsValidRenderSize(SlateRenderSize))
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("RenderWidgetToPng: invalid design/render size %dx%d"),
			SlateRenderSize.X, SlateRenderSize.Y);
		Widget->RemoveFromRoot();
		return false;
	}
	TSharedRef<SWidget> WindowContent = SlateWidget;
	TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
	Window->SetContent(WindowContent);
	FIntPoint EffectiveSize = ComputePreviewRenderSize(SlateRenderSize, Window);
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
	Window->SlatePrepass(1.0f);
	Widget->ForceLayoutPrepass();
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
// Direct WidgetTree rendering before compile, while transient textures are valid.
// ============================================================================

namespace
{
	bool RenderSlateWidgetToPngData(TSharedRef<SWidget> SlateWidget, const FIntPoint& LayoutSize, TArray<uint8>& OutPngData, FIntPoint& OutSize)
	{
		if (!IsValidRenderSize(LayoutSize))
		{
			return false;
		}

		const FIntPoint OutputSize = LayoutSize;
		TSharedRef<SWidget> WindowContent = SlateWidget;

		TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
		Window->SetContent(WindowContent);
		Window->Resize(FVector2D(OutputSize.X, OutputSize.Y));
		Window->SlatePrepass(1.0f);

		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
		RT->Filter = TF_Bilinear;
		RT->ClearColor = FLinearColor::Transparent;
		RT->SRGB = true;
		RT->RenderTargetFormat = RTF_RGBA8;
		RT->InitCustomFormat(OutputSize.X, OutputSize.Y, PF_B8G8R8A8, true);

		FWidgetRenderer Renderer(true);
		Renderer.SetIsPrepassNeeded(false);
		TUniquePtr<FHittestGrid> HitTestGrid = MakeUnique<FHittestGrid>();
		FProceduralShapeGenerator::FlushPendingTextureUpdates();
		FDesignPathRasterizer::FlushPendingTextureUpdates();
		Renderer.DrawWindow(RT, *HitTestGrid, Window, 1.0f, FVector2D(OutputSize.X, OutputSize.Y), 0.1f);
		if (!GUsingNullRHI) { FlushRenderingCommands(); }
		Window->SlatePrepass(1.0f);
		Renderer.DrawWindow(RT, *HitTestGrid, Window, 1.0f, FVector2D(OutputSize.X, OutputSize.Y), 0.1f);
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

		Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), OutputSize.X, OutputSize.Y, ERGBFormat::BGRA, 8);
		OutPngData = Wrapper->GetCompressed();
		OutSize = OutputSize;
		return OutPngData.Num() > 0;
	}
}

bool FUMGWidgetUtils::RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize)
{
	if (!WidgetBP || !WidgetBP->WidgetTree || !WidgetBP->WidgetTree->RootWidget)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("RenderWidgetTreeToPng: invalid WidgetBP or empty WidgetTree"));
		return false;
	}

	if (!FSlateApplication::IsInitialized()) return false;

	// Materialize every SWidget in the WidgetTree and sync its properties.
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

	const FVector2D DesignSize = GetBlueprintDesignSize(WidgetBP);
	const FIntPoint LayoutPixelSize = ResolveDesignLayoutPixelSize(DesignSize);
	if (!(DesignSize.X > 1.0f && DesignSize.Y > 1.0f && IsValidRenderSize(LayoutPixelSize)))
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("RenderWidgetTreeToPng: missing valid DesignTimeSize for %s; source design size is required"),
			*WidgetBP->GetPathName());
		return false;
	}

	UE_LOG(LogDesign2UMG, Log,
		TEXT("RenderWidgetTreeToPng: render source design size %dx%d"),
		LayoutPixelSize.X, LayoutPixelSize.Y);
	return RenderSlateWidgetToPngData(SlateWidget.ToSharedRef(), LayoutPixelSize, OutPngData, OutSize);
}


