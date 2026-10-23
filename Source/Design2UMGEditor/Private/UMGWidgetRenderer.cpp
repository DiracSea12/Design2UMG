// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetRenderer.h"

#include "Design2UMGMaterialAccess.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Design2UMGLog.h"
#include "DesignImageResourceLimits.h"
#include "DesignPathRasterizer.h"
#include "ProceduralShapeGenerator.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/ScopeExit.h"
#include "Misc/Change.h"
#include "Misc/ITransaction.h"

#include "WidgetBlueprint.h"
#include "Design2UMGWidgetMetadata.h"
#include "UObject/Package.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Components/TextBlock.h"
#include "Components/RetainerBox.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Editor.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Input/HittestGrid.h"
#include "Interfaces/ISlate3DRenderer.h"
#include "Types/PaintArgs.h"
#include "Rendering/DrawElements.h"
#include "Rendering/ElementBatcher.h"
#include "Rendering/SlateRenderBatch.h"
#include "Rendering/SlateDrawBuffer.h"
#include "Rendering/SlateRenderer.h"
#include "Misc/App.h"
#include "RHI.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "TextureCompiler.h"
#include "RenderingThread.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/SVirtualWindow.h"
#include "Modules/ModuleManager.h"

// ============================================================================
// Widget rendering
// ============================================================================

namespace
{
	bool RenderSlateWidgetToPngData(TSharedRef<SWidget> SlateWidget, const FIntPoint& LayoutSize, TArray<uint8>& OutPngData, FIntPoint& OutSize, UWidgetTree* WidgetTree = nullptr, FString* OutError = nullptr);
	void DumpOwnedTextBlockFontState(const TCHAR* Stage, UWidgetTree* WidgetTree, const FString& OwnerName)
	{
		FUMGWidgetRenderer::DumpTextBlockFontStates(ELogVerbosity::Log, Stage, WidgetTree, TEXT("owner"), OwnerName);
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
				Brush = &Design2UMGWidgetPropertyAccess::GetBrush(*Image);
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
		FTextureCompilingManager::Get().FinishCompilation({ Texture });
		Texture->FinishCachePlatformData();
		Texture->TemporarilyDisableStreaming();
		Texture->UpdateResource();
		Texture->SetForceMipLevelsToBeResident(30.0f);
		Texture->WaitForStreaming();
		Texture->GetPlatformData();
	}

	void WarmUpMaterialResources(UMaterialInterface* Material)
	{
		if (!Material) return;
		Design2UMGMaterialAccess::WaitForCompilation(*Material->GetMaterial());
		TArray<UTexture*> Textures;
		Design2UMGMaterialAccess::GetUsedTextures(*Material, Textures);
		for (UTexture* Texture : Textures)
		{
			if (UTexture2D* Texture2D = Cast<UTexture2D>(Texture)) WarmUpTextureResource(Texture2D);
		}
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
				WarmUpFontResource(Design2UMGWidgetPropertyAccess::GetFont(*TextBlock));
			}

			const FSlateBrush* Brush = nullptr;
			if (const UImage* Image = Cast<UImage>(Widget))
			{
				Brush = &Design2UMGWidgetPropertyAccess::GetBrush(*Image);
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
				else if (UMaterialInterface* Material = Cast<UMaterialInterface>(Brush->GetResourceObject()))
				{
					WarmUpMaterialResources(Material);
				}
			}
			if (const URetainerBox* Retainer = Cast<URetainerBox>(Widget))
			{
				// The native getter exposes a const view; preparation only completes render caches.
				WarmUpMaterialResources(const_cast<UMaterialInterface*>(Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer)));
			}
		});
	}

	FVector2D GetBlueprintDesignSize(UWidgetBlueprint* WidgetBP)
	{
		return FUMGWidgetRenderer::GetBlueprintDesignTimeSize(WidgetBP).Get(FVector2D::ZeroVector);
	}

	// Slate renders with premultiplied alpha: RGB bytes read back from the render
	// target are already scaled against a transparent black base. Pencil/Figma
	// references store straight alpha, so PNG candidates must be un-premultiplied
	// before compression or every translucent pixel shows a systematic RGB shift.
	uint8 UnpremultiplyChannel(uint8 Premultiplied, uint8 Alpha)
	{
		if (Alpha == 0)
		{
			return 0;
		}
		if (Alpha == 255)
		{
			return Premultiplied;
		}
		const uint32 Scaled = (static_cast<uint32>(Premultiplied) * 255u + Alpha / 2u) / Alpha;
		return static_cast<uint8>(FMath::Min(Scaled, 255u));
	}

	FIntPoint ResolveDesignLayoutPixelSize(const FVector2D& DesignSize)
	{
		return FIntPoint(
			FMath::Max(1, FMath::CeilToInt(DesignSize.X)),
			FMath::Max(1, FMath::CeilToInt(DesignSize.Y)));
	}

	bool IsValidRenderSize(const FIntPoint& Size)
	{
		return Size.X > 0 && Size.Y > 0;
	}

	bool IsWithinExportPixelBudget(const FIntPoint& Size, FString* OutError = nullptr)
	{
		if (static_cast<int64>(Size.X) * Size.Y > FUMGWidgetRenderer::MaxExportPixels)
		{
			if (OutError) *OutError = FString::Printf(TEXT("render export exceeds pixel budget: %dx%d"), Size.X, Size.Y);
			return false;
		}
		return true;
	}

	// The render target must stay within the active RHI's max 2D texture dimension;
	// exceeding it asserts inside the RHI (e.g. D3D12Texture) and kills the editor
	// process. Fail the render structurally instead of submitting the oversized target.
	bool IsRenderSizeWithinRHITextureLimits(const FIntPoint& Size)
	{
		const int32 MaxTextureDimension = static_cast<int32>(GetMax2DTextureDimension());
		return Size.X <= MaxTextureDimension && Size.Y <= MaxTextureDimension;
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

namespace
{


	const TCHAR* ExportBoundsMetadataKey = TEXT("Design2UMG.ExportBounds");

	// Package metadata is not part of the blueprint's reflected transaction state.
	class FExportBoundsChange final : public FSwapChange
	{
	public:
		explicit FExportBoundsChange(FString InValue) : Value(MoveTemp(InValue)) {}
		TUniquePtr<FChange> Execute(UObject* Object) override
		{
			UWidgetBlueprint* Blueprint = CastChecked<UWidgetBlueprint>(Object);
			auto& Metadata = Design2UMGWidgetMetadata::PackageMetadata(Blueprint);
			TUniquePtr<FChange> Inverse = MakeUnique<FExportBoundsChange>(Metadata.GetValue(Blueprint, ExportBoundsMetadataKey));
			Metadata.SetValue(Blueprint, ExportBoundsMetadataKey, *Value);
			return Inverse;
		}
		FString ToString() const override { return TEXT("Source export bounds"); }
	private:
		FString Value;
	};
}

bool FUMGWidgetRenderer::BuildExportPlan(const FIntPoint& Size, int32 MaxTextureDimension, FUMGWidgetExportPlan& OutPlan, FString* OutError)
{
	OutPlan = FUMGWidgetExportPlan();
	if (Size.X <= 0 || Size.Y <= 0 || MaxTextureDimension <= 0) return false;
	if (!IsWithinExportPixelBudget(Size, OutError)) return false;
	OutPlan.Size = Size;
	const bool bSingleTarget = Size.X <= MaxTextureDimension && Size.Y <= MaxTextureDimension;
	const int32 TileEdge = FMath::Min(MaxTextureDimension, DesignImageResources::RenderTileEdge);
	OutPlan.TileSize = bSingleTarget ? Size : FIntPoint(FMath::Min(Size.X, TileEdge), FMath::Min(Size.Y, TileEdge));
	OutPlan.TileCount = FIntPoint(FMath::DivideAndRoundUp(Size.X, OutPlan.TileSize.X), FMath::DivideAndRoundUp(Size.Y, OutPlan.TileSize.Y));
	return true;
}

void FUMGWidgetRenderer::SetBlueprintExportBounds(UWidgetBlueprint* WidgetBlueprint, const TOptional<FBox2D>& Bounds)
{
	auto& Metadata = Design2UMGWidgetMetadata::PackageMetadata(WidgetBlueprint);
	const FString Value = Bounds.IsSet()
		? FVector4(Bounds->Min.X, Bounds->Min.Y, Bounds->Max.X, Bounds->Max.Y).ToString() : FString();
	const FString PreviousValue = Metadata.GetValue(WidgetBlueprint, ExportBoundsMetadataKey);
	if (Value == PreviousValue) return;
	if (GUndo && !GIsTransacting) GUndo->StoreUndo(WidgetBlueprint, MakeUnique<FExportBoundsChange>(PreviousValue));
	Metadata.SetValue(WidgetBlueprint, ExportBoundsMetadataKey, *Value);
}

FBox2D FUMGWidgetRenderer::GetBlueprintExportBounds(UWidgetBlueprint* WidgetBlueprint)
{
	FVector4 Bounds;
	if (Bounds.InitFromString(Design2UMGWidgetMetadata::PackageMetadata(WidgetBlueprint).GetValue(WidgetBlueprint, ExportBoundsMetadataKey)))
		return FBox2D(FVector2D(Bounds.X, Bounds.Y), FVector2D(Bounds.Z, Bounds.W));
	return FBox2D(FVector2D::ZeroVector, GetBlueprintDesignSize(WidgetBlueprint));
}

TOptional<FVector2D> FUMGWidgetRenderer::GetBlueprintDesignTimeSize(UWidgetBlueprint* WidgetBlueprint)
{
	if (WidgetBlueprint && WidgetBlueprint->GeneratedClass)
	{
		if (const UUserWidget* CDO = Cast<UUserWidget>(WidgetBlueprint->GeneratedClass->GetDefaultObject()))
		{
			if (CDO->DesignTimeSize.X > 0.0f && CDO->DesignTimeSize.Y > 0.0f)
			{
				return CDO->DesignTimeSize;
			}
		}
	}

	return TOptional<FVector2D>();
}

void FUMGWidgetRenderer::UnpremultiplyPixels(TArray<FColor>& Pixels)
{
	for (FColor& Pixel : Pixels)
	{
		const uint8 Alpha = Pixel.A;
		Pixel.R = UnpremultiplyChannel(Pixel.R, Alpha);
		Pixel.G = UnpremultiplyChannel(Pixel.G, Alpha);
		Pixel.B = UnpremultiplyChannel(Pixel.B, Alpha);
	}
}

void FUMGWidgetRenderer::DumpTextBlockFontStates(
	const ELogVerbosity::Type Verbosity,
	const TCHAR* Stage,
	UWidgetTree* WidgetTree,
	const TCHAR* OwnerKey,
	const FString& OwnerValue)
{
	if (!WidgetTree)
	{
		return;
	}

	WidgetTree->ForEachWidget([Verbosity, Stage, OwnerKey, &OwnerValue](UWidget* Widget)
	{
		UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
		if (!TextBlock)
		{
			return;
		}

		const FSlateFontInfo FontInfo = Design2UMGWidgetPropertyAccess::GetFont(*TextBlock);
		const UObject* FontObject = FontInfo.FontObject;
		FMsg::Logf(
			__FILE__,
			__LINE__,
			LogDesign2UMG.GetCategoryName(),
			Verbosity,
			TEXT("TextFont[%s]: %s='%s' widget='%s' text='%s' fontObject='%s' typeface='%s' size=%.1f tracking=%d wrap=%.1f autoWrap=%d"),
			Stage,
			OwnerKey,
			*OwnerValue,
			*TextBlock->GetName(),
			*TextBlock->GetText().ToString(),
			FontObject ? *FontObject->GetPathName() : TEXT("<null>"),
			*FontInfo.TypefaceFontName.ToString(),
			static_cast<double>(FontInfo.Size),
			FontInfo.LetterSpacing,
			TextBlock->GetWrapTextAt(),
			TextBlock->GetAutoWrapText() ? 1 : 0);
	});
}

bool FUMGWidgetRenderer::RenderWidgetToPng(
	UWidgetBlueprint* WidgetBP,
	TArray<uint8>& OutPngData,
	FIntPoint& OutSize,
	FString* OutError,
	const TOptional<FVector2D>& RenderSizeOverride,
	const TOptional<FVector2D>& RootExportOffset,
	TStrongObjectPtr<UUserWidget>* OutRenderedWidget,
	TSharedPtr<SWidget>* OutRenderedSlateWidget)
{
	if (OutRenderedWidget)
	{
		OutRenderedWidget->Reset();
	}
	if (OutRenderedSlateWidget)
	{
		OutRenderedSlateWidget->Reset();
	}
	if (OutError)
	{
		OutError->Reset();
	}
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
	DumpOwnedTextBlockFontState(TEXT("compiled-instance"), Widget->WidgetTree, WidgetBP->GetPathName());
	ClearRenderInstanceNavigation(Widget);
	Widget->ForceLayoutPrepass();
	DumpWidgetVisualState(TEXT("compiled-instance"), Widget->WidgetTree, WidgetBP->GetPathName());
	Widget->SetFlags(RF_Transient);
	Widget->AddToRoot();
	ON_SCOPE_EXIT
	{
		Widget->ReleaseSlateResources(true);
		FlushRenderingCommands();
		Widget->RemoveFromRoot();
	};

	TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	// Retainer dynamic materials exist only after the Slate tree is built.
	// Offscreen rendering must finish their shaders and refresh the parent
	// uniforms before drawing; a valid shader map alone does not prepare them.
	TSet<UMaterial*> PreparedRetainerMaterials;
	if (Widget->WidgetTree)
	{
		Widget->WidgetTree->ForEachWidgetAndDescendants([&PreparedRetainerMaterials](UWidget* Child)
		{
			if (URetainerBox* Retainer = Cast<URetainerBox>(Child))
			{
#if UE_VERSION_OLDER_THAN(5, 3, 0)
				// Older UMG disables retained rendering whenever a widget is in design mode.
				// Reapply the asset setting after the Slate subtree exists so previews keep
				// their effects without changing PreConstruct's design-time behavior.
				const bool bRetainRendering = Design2UMGWidgetPropertyAccess::IsRetainRendering(*Retainer);
				Retainer->SetRetainRendering(bRetainRendering);
				if (bRetainRendering)
				{
					Retainer->RequestRender();
				}
#endif

				if (UMaterialInstanceDynamic* Effect = Retainer->GetEffectMaterial())
				{
					UMaterial* Material = Effect->GetMaterial();
					if (!PreparedRetainerMaterials.Contains(Material))
					{
						Design2UMGMaterialAccess::WaitForCompilation(*Material);
						Material->RecacheUniformExpressions(true);
						PreparedRetainerMaterials.Add(Material);
					}
				}
			}
		});
	}
	const FVector2D DesignSize = GetBlueprintDesignSize(WidgetBP);
	const FIntPoint LayoutPixelSize = ResolveDesignLayoutPixelSize(DesignSize);
	const bool bHasValidDesignSize = DesignSize.X > 0.0f && DesignSize.Y > 0.0f && IsValidRenderSize(LayoutPixelSize);
	if (!bHasValidDesignSize)
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("RenderWidgetToPng: missing valid DesignTimeSize for %s; source design size is required"),
			*WidgetBP->GetPathName());
		return false;
	}

	// Canvas calibration: an explicit override pins the virtual window/RT extent;
	// the widget tree keeps laying out against its own geometry.
	const FBox2D ExportBounds = GetBlueprintExportBounds(WidgetBP);
	const FVector2D ExportSize = ExportBounds.GetSize();
	const FIntPoint SlateRenderSize = RenderSizeOverride.IsSet()
		? FIntPoint(FMath::RoundToInt(RenderSizeOverride.GetValue().X), FMath::RoundToInt(RenderSizeOverride.GetValue().Y))
		: FIntPoint(FMath::CeilToInt(ExportSize.X), FMath::CeilToInt(ExportSize.Y));
	if (!IsValidRenderSize(SlateRenderSize))
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("RenderWidgetToPng: invalid design/render size %dx%d"),
			SlateRenderSize.X, SlateRenderSize.Y);
		return false;
	}
	FString ExportError;
	if (!IsWithinExportPixelBudget(SlateRenderSize, &ExportError))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("RenderWidgetToPng: %s"), *ExportError);
		if (OutError) *OutError = ExportError;
		return false;
	}
	// Keep the root at its design extent when the export canvas includes overflow.
	// Use the source export offset when present; otherwise center two-axis overflow and
	// top-left-anchor one-axis overflow. Without overflow, let the root fill the canvas.
	// Point anchors keep the extent independent of desired size.
	TSharedRef<SWidget> WindowContent = SlateWidget;
	const bool bCalibrationXOverflow = SlateRenderSize.X > LayoutPixelSize.X;
	const bool bCalibrationYOverflow = SlateRenderSize.Y > LayoutPixelSize.Y;
	if (RootExportOffset.IsSet() || (!RenderSizeOverride.IsSet() && !ExportBounds.Min.IsNearlyZero()) || bCalibrationXOverflow || bCalibrationYOverflow)
	{
		FVector2D Offset = FVector2D::ZeroVector;
		if (RootExportOffset.IsSet())
		{
			Offset = RootExportOffset.GetValue();
		}
		else if (!RenderSizeOverride.IsSet())
		{
			Offset = -ExportBounds.Min;
		}
		else if (bCalibrationXOverflow && bCalibrationYOverflow)
		{
			Offset = FVector2D(
				(SlateRenderSize.X - LayoutPixelSize.X) / 2.0f,
				(SlateRenderSize.Y - LayoutPixelSize.Y) / 2.0f);
		}
		WindowContent = SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(0.f, 0.f))
			.Offset(FMargin(Offset.X, Offset.Y, static_cast<float>(LayoutPixelSize.X), static_cast<float>(LayoutPixelSize.Y)))
			.Alignment(FVector2D::ZeroVector)
			.AutoSize(false)
			[SlateWidget];
	}
	if (!IsRenderSizeWithinRHITextureLimits(SlateRenderSize))
	{
		const bool bRendered = RenderSlateWidgetToPngData(WindowContent, SlateRenderSize, OutPngData, OutSize, Widget->WidgetTree, OutError);
		if (bRendered && OutRenderedWidget) OutRenderedWidget->Reset(Widget);
		if (bRendered && OutRenderedSlateWidget) *OutRenderedSlateWidget = SlateWidget;
		if (!bRendered && OutError && OutError->IsEmpty()) *OutError = TEXT("Failed to render tiled source canvas");
		return bRendered;
	}
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
		RT->ReleaseResource();
		RT->MarkAsGarbage();
		return false;
	}

	TArray<FColor> Pixels;
	Resource->ReadPixels(Pixels);
	FUMGWidgetRenderer::UnpremultiplyPixels(Pixels);
	RT->ReleaseResource();
	RT->MarkAsGarbage();

	if (Pixels.Num() == 0) return false;

	IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid()) return false;

	Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), EffectiveSize.X, EffectiveSize.Y, ERGBFormat::BGRA, 8);
	OutPngData = Wrapper->GetCompressed();
	OutSize = EffectiveSize;
	const bool bRendered = OutPngData.Num() > 0;
	if (bRendered && OutRenderedWidget)
	{
		OutRenderedWidget->Reset(Widget);
	}
	if (bRendered && OutRenderedSlateWidget)
	{
		*OutRenderedSlateWidget = SlateWidget;
	}
	return bRendered;
}

// ============================================================================
// Direct WidgetTree rendering before compile, while transient textures are valid.
// ============================================================================

namespace
{
	bool RenderTiledSlateWidgetToPixels(TSharedRef<SWidget> Content, const FIntPoint& Size, TArray<FColor>& Pixels, UWidgetTree* WidgetTree, FString* OutError)
	{
		FUMGWidgetExportPlan Plan;
		if (!FUMGWidgetRenderer::BuildExportPlan(Size, static_cast<int32>(GetMax2DTextureDimension()), Plan, OutError)) return false;
		TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
		Window->SetContent(Content);
		Window->Resize(FVector2D(Size));
		Window->SlatePrepass(1.0f);
		FWidgetRenderer Renderer(true);
		ISlate3DRenderer* SlateRenderer = Renderer.GetSlateRenderer();
		FSlateRenderer* MainRenderer = FSlateApplication::Get().GetRenderer();
		FHittestGrid HitTestGrid;
		HitTestGrid.SetHittestArea(FVector2D::ZeroVector, FVector2D(Size));
		const FGeometry Geometry = FGeometry::MakeRoot(FVector2D(Size), FSlateLayoutTransform(1.0f));
		const FSlateRect Clip(0, 0, Size.X, Size.Y);
		FProceduralShapeGenerator::FlushPendingTextureUpdates();
		FDesignPathRasterizer::FlushPendingTextureUpdates();
		// Warm the native Retainers at full logical geometry, without a full-sized output target.
		{
			ISlate3DRenderer::FScopedAcquireDrawBuffer Warmup(*SlateRenderer);
			FSlateWindowElementList& Elements = Warmup.GetDrawBuffer().AddWindowElementList(Window);
			FScopeLock ScopeLock(MainRenderer->GetResourceCriticalSection());
			Window->Paint(FPaintArgs(nullptr, HitTestGrid, FVector2D::ZeroVector, FApp::GetCurrentTime(), 0.1f), Geometry, Clip, Elements, 0, FWidgetStyle(), Window->IsEnabled());
		}
		FlushRenderingCommands();
		Window->SlatePrepass(1.0f);
		HitTestGrid.Clear();
		ISlate3DRenderer::FScopedAcquireDrawBuffer Frame(*SlateRenderer);
		FSlateDrawBuffer& DrawBuffer = Frame.GetDrawBuffer();
		FSlateWindowElementList& Elements = DrawBuffer.AddWindowElementList(Window);
		const double PaintTime = FApp::GetCurrentTime();
		const double RealTime = FPlatformTime::Seconds() - GStartTime;
		const float DeltaTime = static_cast<float>(FApp::GetDeltaTime());
		{
			FScopeLock ScopeLock(MainRenderer->GetResourceCriticalSection());
			Window->Paint(FPaintArgs(nullptr, HitTestGrid, FVector2D::ZeroVector, PaintTime, 0.1f), Geometry, Clip, Elements, 0, FWidgetStyle(), Window->IsEnabled());
			SlateRenderer->DrawWindow_GameThread(DrawBuffer);
		}
		// A root-window postprocess reads neighboring pixels from the output target.
		// Replaying it on independent tiles would lose those samples at tile edges.
		for (const FSlateRenderBatch& Batch : Elements.GetBatchData().GetRenderBatches())
		{
			if (Batch.GetShaderType() == ESlateShader::PostProcess)
			{
				if (OutError) *OutError = TEXT("Tiled export requires cross-tile background-blur sampling; native postprocess is not yet supported");
				FlushRenderingCommands();
				return false;
			}
		}
		// A native Retainer falls back to unfiltered children when its target is oversized.
		// Report that unsupported capture instead of exporting a silently missing effect.
		bool bCaptureFits = true;
		if (WidgetTree) WidgetTree->ForEachWidgetAndDescendants([&](UWidget* Widget)
		{
			if (const URetainerBox* Retainer = Cast<URetainerBox>(Widget); Retainer && Design2UMGWidgetPropertyAccess::IsRetainRendering(*Retainer))
			{
				const FGeometry& Cached = Retainer->GetCachedGeometry();
				const auto Scale = Cached.GetAccumulatedRenderTransform().GetMatrix().GetScale().GetVector();
				const FVector2D Capture = FVector2D(Cached.GetLocalSize()) * FVector2D(Scale);
				const FIntPoint CaptureSize(FMath::RoundToInt(FMath::Abs(Capture.X)), FMath::RoundToInt(FMath::Abs(Capture.Y)));
				if (!IsRenderSizeWithinRHITextureLimits(CaptureSize))
				{
					bCaptureFits = false;
					if (OutError) *OutError = FString::Printf(TEXT("Retainer capture exceeds RHI texture dimension: %dx%d (%s)"), CaptureSize.X, CaptureSize.Y, *Retainer->GetName());
				}
			}
		});
		if (!bCaptureFits)
		{
			FlushRenderingCommands();
			return false;
		}
		Pixels.SetNumUninitialized(Size.X * Size.Y);
		for (int32 Y = 0; Y < Size.Y; Y += Plan.TileSize.Y)
		{
			for (int32 X = 0; X < Size.X; X += Plan.TileSize.X)
			{
				const FIntPoint TileSize(FMath::Min(Plan.TileSize.X, Size.X - X), FMath::Min(Plan.TileSize.Y, Size.Y - Y));
				TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
				Target->Filter = TF_Bilinear;
				Target->ClearColor = FLinearColor::Transparent;
				Target->SRGB = true;
				Target->RenderTargetFormat = RTF_RGBA8;
				Target->InitCustomFormat(TileSize.X, TileSize.Y, PF_B8G8R8A8, true);
				FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
				if (!Resource) return false;
				// Only the projection viewport moves. Layout, clips, paint data and shader time stay fixed.
				DrawBuffer.ViewOffset = FVector2D(-X, -Y);
				FRenderThreadUpdateContext Context = { &DrawBuffer, PaintTime - GStartTime, DeltaTime, RealTime, DeltaTime, Resource, SlateRenderer, true };
				MainRenderer->AddWidgetRendererUpdate(Context, false);
				FlushRenderingCommands();
				TArray<FColor> TilePixels;
				const bool bRead = Resource->ReadPixels(TilePixels);
				Target->ReleaseResource();
				if (!bRead || TilePixels.Num() != TileSize.X * TileSize.Y) return false;
				FUMGWidgetRenderer::UnpremultiplyPixels(TilePixels);
				for (int32 Row = 0; Row < TileSize.Y; ++Row)
					FMemory::Memcpy(&Pixels[(Y + Row) * Size.X + X], &TilePixels[Row * TileSize.X], TileSize.X * sizeof(FColor));
			}
		}
		return true;
	}

	bool RenderSlateWidgetToPixels(TSharedRef<SWidget> SlateWidget, const FIntPoint& LayoutSize, TArray<FColor>& Pixels, UWidgetTree* WidgetTree = nullptr, FString* OutError = nullptr)
	{
		if (!IsValidRenderSize(LayoutSize))
		{
			return false;
		}

		if (!IsWithinExportPixelBudget(LayoutSize)) return false;
		if (!IsRenderSizeWithinRHITextureLimits(LayoutSize))
			return RenderTiledSlateWidgetToPixels(SlateWidget, LayoutSize, Pixels, WidgetTree, OutError);

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

		const bool bReadPixels = Resource->ReadPixels(Pixels);
		FUMGWidgetRenderer::UnpremultiplyPixels(Pixels);
		RT->ReleaseResource();
		RT->MarkAsGarbage();

		return bReadPixels && Pixels.Num() == OutputSize.X * OutputSize.Y;
	}

	bool RenderSlateWidgetToPngData(TSharedRef<SWidget> SlateWidget, const FIntPoint& Size, TArray<uint8>& OutPngData, FIntPoint& OutSize, UWidgetTree* WidgetTree, FString* OutError)
	{
		TArray<FColor> Pixels;
		if (!RenderSlateWidgetToPixels(SlateWidget, Size, Pixels, WidgetTree, OutError)) return false;
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8)) return false;
		OutPngData = Wrapper->GetCompressed();
		OutSize = Size;
		return OutPngData.Num() > 0;
	}

}

bool FUMGWidgetRenderer::RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, TArray<uint8>& OutPngData, FIntPoint& OutSize)
{
	if (!WidgetBP || !WidgetBP->WidgetTree || !WidgetBP->WidgetTree->RootWidget)
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("RenderWidgetTreeToPng: invalid WidgetBP or empty WidgetTree"));
		return false;
	}

	if (!FSlateApplication::IsInitialized()) return false;
	ON_SCOPE_EXIT
	{
		WidgetBP->WidgetTree->ForEachWidget([](UWidget* Widget) { Widget->ReleaseSlateResources(true); });
		FlushRenderingCommands();
	};

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
	if (!(DesignSize.X > 0.0f && DesignSize.Y > 0.0f && IsValidRenderSize(LayoutPixelSize)))
	{
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("RenderWidgetTreeToPng: missing valid DesignTimeSize for %s; source design size is required"),
			*WidgetBP->GetPathName());
		return false;
	}

	UE_LOG(LogDesign2UMG, Log,
		TEXT("RenderWidgetTreeToPng: render source design size %dx%d"),
		LayoutPixelSize.X, LayoutPixelSize.Y);
	const FBox2D ExportBounds = GetBlueprintExportBounds(WidgetBP);
	const FVector2D ExportSize = ExportBounds.GetSize();
	TSharedRef<SWidget> Content = SlateWidget.ToSharedRef();
	if (!ExportBounds.Min.IsNearlyZero() || !ExportSize.Equals(FVector2D(LayoutPixelSize)))
	{
		Content = SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(0.f, 0.f))
			.Offset(FMargin(-ExportBounds.Min.X, -ExportBounds.Min.Y, LayoutPixelSize.X, LayoutPixelSize.Y))
			.Alignment(FVector2D::ZeroVector)
			.AutoSize(false)
			[Content];
	}
	return RenderSlateWidgetToPngData(Content, FIntPoint(FMath::CeilToInt(ExportSize.X), FMath::CeilToInt(ExportSize.Y)), OutPngData, OutSize, WidgetBP->WidgetTree);
}
