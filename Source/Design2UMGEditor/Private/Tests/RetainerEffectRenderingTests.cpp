// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGMaterialAccess.h"
#include "Design2UMGWidgetPropertyAccess.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/RetainerBox.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Design2UMGEffectMaterialBuilder.h"
#include "DesignBlueprintRenderPreparation.h"
#include "UMGGenerator.h"
#include "UMGWidgetRenderer.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Input/HittestGrid.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UMGWidgetTreeBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SVirtualWindow.h"
#include "WidgetBlueprint.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	constexpr int32 ImageSize = 64;

	UTexture2D* CreateRetainerSourceTexture()
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(ImageSize, ImageSize, PF_B8G8R8A8);
		if (!Texture) return nullptr;
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->NeverStream = true;
		Texture->MipGenSettings = TMGS_NoMipmaps;
		FColor* Pixels = static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < ImageSize; ++Y)
		{
			for (int32 X = 0; X < ImageSize; ++X)
			{
				const uint8 Alpha = X + Y < 32 ? 0 : X + Y < 64 ? 96 : X + Y < 96 ? 192 : 255;
				Pixels[Y * ImageSize + X] = FColor(uint8(80 + X), uint8(140 + Y), 200, Alpha);
			}
		}
		Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
		Texture->UpdateResource();
		return Texture;
	}

	URetainerBox* CreateEffectRetainer(UTexture2D* Texture, UMaterialInterface* Parent, bool bTypedProperties, bool bInnerShadow, bool bGeometryMask)
	{
		URetainerBox* Retainer = NewObject<URetainerBox>(GetTransientPackage());
		UImage* Image = NewObject<UImage>(Retainer);
		Image->SetBrushFromTexture(Texture, true);
		Retainer->SetContent(Image);
		if (bTypedProperties)
		{
			FUMGWidgetNode Node;
			Node.Type = TEXT("RetainerBox");
			Node.Effect.bRetainRendering = true;
			Node.Effect.TextureParameter = TEXT("Texture");
			Node.Effect.EffectMaterial = Parent->GetPathName();
			Node.Effect.EffectBlurUVStep = FVector2D(0.0325f, 0.06125f);
			if (bGeometryMask)
			{
				Node.Effect.GeometryMaskShape = EUMGGeometryMaskShape::RoundedRectangle;
				Node.Effect.GeometryMaskSize = FVector2D(ImageSize, ImageSize);
				Node.Effect.GeometryMaskCornerRadii = FVector4(8.0f, 16.0f, 12.0f, 4.0f);
			}
			if (bInnerShadow)
			{
				Node.Effect.EffectColor = FLinearColor(1.0f, 0.0f, 1.0f, 0.2f);
				Node.Effect.EffectOffset = FVector2D(-0.04f, -0.015625f);
			}
			FUMGWidgetTreeBuilder::ApplyWidgetProperties(Retainer, Node);
		}
		else
		{
			Retainer->SetRetainRendering(true);
			Retainer->SetTextureParameter(TEXT("Texture"));
			Retainer->SetEffectMaterial(Parent);
			Retainer->TakeWidget();
			UMaterialInstanceDynamic* Effect = Retainer->GetEffectMaterial();
			if (!Effect) return nullptr;
			Effect->SetVectorParameterValue(TEXT("BlurUVStep"), FLinearColor(0.0325f, 0.06125f, 0.0f, 0.0f));
			if (bGeometryMask)
			{
				Effect->SetScalarParameterValue(TEXT("MaskShape"), 1.0f);
				Effect->SetVectorParameterValue(TEXT("MaskSize"), FLinearColor(ImageSize, ImageSize, 0.0f, 0.0f));
				Effect->SetVectorParameterValue(TEXT("MaskCornerRadiiTop"), FLinearColor(8.0f, 16.0f, 0.0f, 0.0f));
				Effect->SetVectorParameterValue(TEXT("MaskCornerRadiiBottom"), FLinearColor(12.0f, 4.0f, 0.0f, 0.0f));
			}
			if (bInnerShadow)
			{
				Effect->SetVectorParameterValue(TEXT("InnerShadowColor"), FLinearColor(1.0f, 0.0f, 1.0f, 0.2f));
				Effect->SetScalarParameterValue(TEXT("InnerShadowStrength"), 0.2f);
				Effect->SetVectorParameterValue(TEXT("InnerShadowOffset"), FLinearColor(-0.04f, -0.015625f, 0.0f, 0.0f));
			}
		}
		return Retainer;
	}

	bool DrawEffectRetainer(URetainerBox* Retainer, TArray<FColor>& Pixels)
	{
		TSharedRef<SVirtualWindow> Window = SNew(SVirtualWindow);
		Window->SetContent(Retainer->TakeWidget());
		Window->Resize(FVector2D(ImageSize, ImageSize));
		Window->SlatePrepass(1.0f);
		TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
		Target->ClearColor = FLinearColor::Transparent;
		Target->InitCustomFormat(ImageSize, ImageSize, PF_B8G8R8A8, true);
		FWidgetRenderer Renderer(true);
		Renderer.SetIsPrepassNeeded(false);
		FHittestGrid HitTestGrid;
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			Retainer->RequestRender();
			Renderer.DrawWindow(Target.Get(), HitTestGrid, Window, 1.0f, FVector2D(ImageSize, ImageSize), 0.1f);
			FlushRenderingCommands();
		}
		FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
		const bool bRead = Resource && Resource->ReadPixels(Pixels) && Pixels.Num() == ImageSize * ImageSize;
		Target->ReleaseResource();
		FlushRenderingCommands();
		return bRead;
	}

	int32 CountPixelDifferences(const TArray<FColor>& A, const TArray<FColor>& B)
	{
		if (A.Num() != B.Num()) return INDEX_NONE;
		int32 Count = 0;
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index] != B[Index]) ++Count;
		}
		return Count;
	}

	bool ReadRenderedPixels(const TArray<uint8>& Png, TArray<FColor>& Pixels)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Image = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Image || !Image->SetCompressed(Png.GetData(), Png.Num())) return false;
		TArray64<uint8> Raw;
		if (!Image->GetRaw(ERGBFormat::BGRA, 8, Raw)
			|| Image->GetWidth() != ImageSize || Image->GetHeight() != ImageSize
			|| Raw.Num() != ImageSize * ImageSize * sizeof(FColor)) return false;
		Pixels.SetNumUninitialized(ImageSize * ImageSize);
		FMemory::Memcpy(Pixels.GetData(), Raw.GetData(), Raw.Num());
		return true;
	}

	bool ReadRenderedPixel(const TArray<uint8>& Png, FColor& Pixel, int32 X = ImageSize / 2, int32 Y = ImageSize / 2)
	{
		TArray<FColor> Pixels;
		if (!ReadRenderedPixels(Png, Pixels)) return false;
		Pixel = Pixels[Y * ImageSize + X];
		return true;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerEffectRenderingTest,
	"Design2UMG.PropertyApplier.RetainerEffectRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerEffectRenderingTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddError(TEXT("Retainer effect rendering requires real graphics backend"));
		return false;
	}
	TStrongObjectPtr<UTexture2D> Texture(CreateRetainerSourceTexture());
	if (!TestNotNull(TEXT("Retainer source texture"), Texture.Get())) return false;
	for (int32 EffectIndex = 0; EffectIndex < 3; ++EffectIndex)
	{
		const bool bInnerShadow = EffectIndex == 1;
		const bool bGeometryMask = EffectIndex == 2;
		UMaterialInterface* Parent = bGeometryMask
			? Design2UMGEffectMaterialBuilder::GetOrCreateGeometryMaskRetainerMaterial(
				EUMGGeometryMaskShape::RoundedRectangle, FVector2D(ImageSize, ImageSize), FVector4(8.0f, 16.0f, 12.0f, 4.0f))
			: bInnerShadow ? Design2UMGEffectMaterialBuilder::GetOrCreateInnerShadowRetainerMaterial()
			: Design2UMGEffectMaterialBuilder::GetOrCreateForegroundBlurRetainerMaterial();
		if (!TestNotNull(TEXT("Saved effect parent"), Parent)) return false;
		TStrongObjectPtr<UMaterial> PreviewParent(DuplicateObject<UMaterial>(Parent->GetMaterial(), GetTransientPackage()));
		PreviewParent->ClearFlags(RF_Public | RF_Standalone);
		PreviewParent->SetFlags(RF_Transient);
		PreviewParent->bIsPreviewMaterial = true;
		PreviewParent->PreEditChange(nullptr);
		PreviewParent->PostEditChange();
		Design2UMGMaterialAccess::CacheShadersForRendering(*PreviewParent);
		Design2UMGMaterialAccess::WaitForCompilation(*PreviewParent);

		TStrongObjectPtr<URetainerBox> Runtime(CreateEffectRetainer(Texture.Get(), Parent, false, bInnerShadow, bGeometryMask));
		TStrongObjectPtr<URetainerBox> Typed(CreateEffectRetainer(Texture.Get(), Parent, true, bInnerShadow, bGeometryMask));
		TStrongObjectPtr<URetainerBox> Preview(CreateEffectRetainer(Texture.Get(), PreviewParent.Get(), false, bInnerShadow, bGeometryMask));
		if (!TestNotNull(TEXT("Runtime retainer"), Runtime.Get())
			|| !TestNotNull(TEXT("Typed retainer"), Typed.Get())
			|| !TestNotNull(TEXT("Preview retainer"), Preview.Get())) return false;
		TArray<FColor> RuntimePixels, TypedPixels, PreviewPixels;
		if (!TestTrue(TEXT("Render runtime retainer"), DrawEffectRetainer(Runtime.Get(), RuntimePixels))
			|| !TestTrue(TEXT("Render typed retainer"), DrawEffectRetainer(Typed.Get(), TypedPixels))
			|| !TestTrue(TEXT("Render preview retainer"), DrawEffectRetainer(Preview.Get(), PreviewPixels))) return false;
		TestTrue(TEXT("Rendered source contains partial alpha"),
			RuntimePixels.ContainsByPredicate([](const FColor& Pixel) { return Pixel.A > 0 && Pixel.A < 255; }));
		const FString Label = bGeometryMask ? TEXT("geometry mask") : bInnerShadow ? TEXT("inner shadow") : TEXT("foreground blur");
		const int32 TypedDelta = CountPixelDifferences(RuntimePixels, TypedPixels);
		const int32 PreviewDelta = CountPixelDifferences(RuntimePixels, PreviewPixels);
		AddInfo(FString::Printf(TEXT("%s typed/runtime differences=%d saved/preview differences=%d"),
			*Label, TypedDelta, PreviewDelta));
		TestEqual(Label + TEXT(" persisted parameters retain runtime rendering"), TypedDelta, 0);
		TestEqual(Label + TEXT(" saved parent retains preview rendering"), PreviewDelta, 0);
		Runtime->ReleaseSlateResources(true);
		Typed->ReleaseSlateResources(true);
		Preview->ReleaseSlateResources(true);
		FlushRenderingCommands();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerBlueprintRenderingTest,
	"Design2UMG.PropertyApplier.RetainerBlueprintRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerBlueprintRenderingTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddError(TEXT("Blueprint retainer rendering requires real graphics backend"));
		return false;
	}
	const FString Folder = TEXT("/Game/Design2UMG/Automation/RetainerRendering_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	for (bool bMasked : {false, true})
	{
		FUMGWidgetNode Fill;
		Fill.Type = TEXT("Border");
		Fill.Id = TEXT("fill");
		Fill.Name = TEXT("Fill");
		Fill.Bounds.W = ImageSize;
		Fill.Bounds.H = ImageSize;
		Fill.Brush.DrawAs = ESlateBrushDrawType::Image;
		Fill.Brush.BrushColor = FLinearColor::Blue;
		FUMGWidgetData Data;
		Data.WidgetName = bMasked ? TEXT("WBP_Masked") : TEXT("WBP_Plain");
		Data.RootWidget = Fill;
		if (bMasked)
		{
			Data.RootWidget = FUMGWidgetNode();
			Data.RootWidget.Type = TEXT("RetainerBox");
			Data.RootWidget.Id = TEXT("mask");
			Data.RootWidget.Name = TEXT("Mask");
			Data.RootWidget.Bounds.W = ImageSize;
			Data.RootWidget.Bounds.H = ImageSize;
			Data.RootWidget.Effect.bRetainRendering = true;
			Data.RootWidget.Effect.TextureParameter = TEXT("Texture");
			Data.RootWidget.Effect.EffectMaterial = TEXT("d2u.retainer.geometry_mask");
			Data.RootWidget.Effect.GeometryMaskShape = EUMGGeometryMaskShape::RoundedRectangle;
			Data.RootWidget.Effect.GeometryMaskSize = FVector2D(ImageSize, ImageSize);
			Data.RootWidget.Effect.GeometryMaskCornerRadii = FVector4(8.0f, 8.0f, 8.0f, 8.0f);
			Data.RootWidget.Children.Add(Fill);
		}
		UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, Folder);
		if (!TestNotNull(TEXT("Built fixture blueprint"), Blueprint)) return false;
		if (!TestTrue(TEXT("Save fixture blueprint"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess())) return false;
		if (!TestNotNull(TEXT("Saved fixture blueprint"), Blueprint)) return false;
		Blueprint = DesignBlueprintRenderPreparation::ReloadWidgetBlueprintForRender(Blueprint);
		if (!TestNotNull(TEXT("Reloaded fixture blueprint"), Blueprint)) return false;
		TArray<uint8> Png;
		FIntPoint Size;
		FString Error;
		TStrongObjectPtr<UUserWidget> RenderedWidget;
		if (!TestTrue(TEXT("Render saved blueprint through production renderer"),
			FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, Png, Size, &Error, {}, {}, &RenderedWidget)))
		{
			AddError(Error);
			return false;
		}
		FColor Center;
		if (!TestTrue(TEXT("Decode rendered center"), ReadRenderedPixel(Png, Center))) return false;
		AddInfo(FString::Printf(TEXT("masked=%d center=%s"), bMasked, *Center.ToString()));
		TestEqual(TEXT("Saved blueprint preserves opaque blue center"), Center, FColor(0, 0, 255, 255));
		for (const FIntPoint Corner : {FIntPoint(0, 0), FIntPoint(ImageSize - 1, 0),
			FIntPoint(0, ImageSize - 1), FIntPoint(ImageSize - 1, ImageSize - 1)})
		{
			FColor Pixel;
			if (!TestTrue(TEXT("Decode rendered corner"), ReadRenderedPixel(Png, Pixel, Corner.X, Corner.Y))) return false;
			TestEqual(TEXT("Saved blueprint preserves rounded mask corners"), Pixel.A, uint8(bMasked ? 0 : 255));
		}
		if (bMasked)
		{
			URetainerBox* Retainer = Cast<URetainerBox>(RenderedWidget->WidgetTree->RootWidget);
			const UMaterialInterface* Material = Retainer ? Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer) : nullptr;
			if (!TestNotNull(TEXT("Compiled retainer effect"), Material)) return false;
			TestTrue(TEXT("Effect remains a saved asset rather than a WidgetTree-owned object"),
				!Material->HasAnyFlags(RF_Transient) && Material->GetOuter() == Material->GetOutermost()
				&& FPackageName::DoesPackageExist(Material->GetOutermost()->GetName()));
			FColor Edge;
			if (!TestTrue(TEXT("Decode rendered straight edge"), ReadRenderedPixel(Png, Edge, ImageSize / 2, 0))) return false;
			TestTrue(TEXT("Mask keeps its straight edge opaque"), Edge.A > 200 && Edge.B > 200);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerNestedChildBlueprintRenderingTest,
	"Design2UMG.PropertyApplier.RetainerNestedChildBlueprintRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerNestedChildBlueprintRenderingTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddError(TEXT("Nested child blueprint retainer rendering requires a real graphics backend"));
		return false;
	}

	for (const bool bRetainRendering : {false, true})
	{
		const FString Folder = FString(TEXT("/Game/Design2UMG/Automation/RetainerNestedChild_"))
			+ (bRetainRendering ? TEXT("Retained_") : TEXT("PassThrough_"))
			+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const bool bBranchSucceeded = [this, &Folder, bRetainRendering]()
		{
			FUMGWidgetNode Fill;
			Fill.Type = TEXT("Border");
			Fill.Id = TEXT("fill");
			Fill.Name = TEXT("Fill");
			Fill.Bounds.W = ImageSize;
			Fill.Bounds.H = ImageSize;
			Fill.Brush.DrawAs = ESlateBrushDrawType::Image;
			Fill.Brush.BrushColor = FLinearColor::Blue;

			FUMGWidgetNode Retainer;
			Retainer.Type = TEXT("RetainerBox");
			Retainer.Id = TEXT("mask");
			Retainer.Name = TEXT("Mask");
			Retainer.Bounds.W = ImageSize;
			Retainer.Bounds.H = ImageSize;
			Retainer.Slot.Offsets.Right = ImageSize;
			Retainer.Slot.Offsets.Bottom = ImageSize;
			Retainer.Effect.bRetainRendering = bRetainRendering;
			Retainer.Effect.TextureParameter = TEXT("Texture");
			Retainer.Effect.EffectMaterial = TEXT("d2u.retainer.geometry_mask");
			Retainer.Effect.GeometryMaskShape = EUMGGeometryMaskShape::RoundedRectangle;
			Retainer.Effect.GeometryMaskSize = FVector2D(ImageSize, ImageSize);
			Retainer.Effect.GeometryMaskCornerRadii = FVector4(8.0f, 8.0f, 8.0f, 8.0f);
			Retainer.Children.Add(Fill);

			FUMGWidgetData ChildData;
			ChildData.WidgetName = bRetainRendering ? TEXT("WBP_ChildRetained") : TEXT("WBP_ChildPassThrough");
			ChildData.RootWidget.Type = TEXT("CanvasPanel");
			ChildData.RootWidget.Id = TEXT("child_root");
			ChildData.RootWidget.Name = TEXT("ChildRoot");
			ChildData.RootWidget.Bounds.W = ImageSize;
			ChildData.RootWidget.Bounds.H = ImageSize;
			ChildData.RootWidget.Children.Add(Retainer);

			UWidgetBlueprint* ChildBlueprint = FUMGGenerator::BuildWidgetBlueprint(ChildData, Folder);
			if (!TestNotNull(TEXT("Build nested child blueprint"), ChildBlueprint)
				|| !TestTrue(TEXT("Save nested child blueprint"), FUMGGenerator::SaveWidgetBlueprint(ChildBlueprint).IsSuccess()))
			{
				return false;
			}
			ChildBlueprint = DesignBlueprintRenderPreparation::ReloadWidgetBlueprintForRender(ChildBlueprint);
			if (!TestNotNull(TEXT("Reload nested child blueprint"), ChildBlueprint)) return false;

			FUMGWidgetData ParentData;
			ParentData.WidgetName = bRetainRendering ? TEXT("WBP_ParentRetained") : TEXT("WBP_ParentPassThrough");
			ParentData.RootWidget.Type = TEXT("Overlay");
			ParentData.RootWidget.Id = TEXT("parent_root");
			ParentData.RootWidget.Name = TEXT("ParentRoot");
			ParentData.RootWidget.Bounds.W = ImageSize;
			ParentData.RootWidget.Bounds.H = ImageSize;

			UWidgetBlueprint* ParentBlueprint = FUMGGenerator::BuildWidgetBlueprint(ParentData, Folder);
			if (!TestNotNull(TEXT("Build parent blueprint"), ParentBlueprint)) return false;
			UUserWidget* ChildTemplate = ParentBlueprint->WidgetTree->ConstructWidget<UUserWidget>(
				TSubclassOf<UUserWidget>(ChildBlueprint->GeneratedClass.Get()), TEXT("RetainerChild"));
			if (!TestNotNull(TEXT("Create child blueprint template"), ChildTemplate)) return false;
			UPanelWidget* ParentRoot = Cast<UPanelWidget>(ParentBlueprint->WidgetTree->RootWidget);
			if (!TestNotNull(TEXT("Parent blueprint has a panel root"), ParentRoot)) return false;
			ParentRoot->AddChild(ChildTemplate);
			if (!TestTrue(TEXT("Save parent blueprint"), FUMGGenerator::SaveWidgetBlueprint(ParentBlueprint).IsSuccess()))
			{
				return false;
			}
			ParentBlueprint = DesignBlueprintRenderPreparation::ReloadWidgetBlueprintForRender(ParentBlueprint);
			if (!TestNotNull(TEXT("Reload parent blueprint"), ParentBlueprint)) return false;

			TArray<uint8> Png;
			FIntPoint Size;
			FString Error;
			TStrongObjectPtr<UUserWidget> RenderedWidget;
			if (!TestTrue(TEXT("Render parent with nested child retainer"),
				FUMGWidgetRenderer::RenderWidgetToPng(ParentBlueprint, Png, Size, &Error, {}, {}, &RenderedWidget)))
			{
				AddError(Error);
				return false;
			}
			if (!TestTrue(TEXT("Rendered parent remains design time"),
				RenderedWidget.IsValid() && RenderedWidget->IsDesignTime())
				|| !TestTrue(TEXT("Rendered parent keeps ExecutePreConstruct"),
					RenderedWidget->HasAnyDesignerFlags(EWidgetDesignFlags::ExecutePreConstruct)))
			{
				return false;
			}

			UUserWidget* RenderedChild = RenderedWidget.IsValid()
				? Cast<UUserWidget>(RenderedWidget->WidgetTree->FindWidget(TEXT("RetainerChild")))
				: nullptr;
			if (!TestNotNull(TEXT("Rendered parent contains child blueprint"), RenderedChild)) return false;
			if (!TestTrue(TEXT("Rendered child remains design time"), RenderedChild->IsDesignTime())
				|| !TestTrue(TEXT("Rendered child keeps ExecutePreConstruct"),
					RenderedChild->HasAnyDesignerFlags(EWidgetDesignFlags::ExecutePreConstruct)))
			{
				return false;
			}
			URetainerBox* RenderedRetainer = Cast<URetainerBox>(RenderedChild->WidgetTree->FindWidget(TEXT("Mask")));
			if (!TestNotNull(TEXT("Rendered child contains nested retainer"), RenderedRetainer)) return false;
			TestEqual(TEXT("Preview preserves child retainer setting"),
				Design2UMGWidgetPropertyAccess::IsRetainRendering(*RenderedRetainer), bRetainRendering);

			FColor Center;
			FColor Corner;
			if (!TestTrue(TEXT("Decode nested retainer center"), ReadRenderedPixel(Png, Center))
				|| !TestTrue(TEXT("Decode nested retainer corner"), ReadRenderedPixel(Png, Corner, 0, 0)))
			{
				return false;
			}
			TestEqual(TEXT("Nested child retainer keeps opaque content"), Center, FColor(0, 0, 255, 255));
			TestEqual(TEXT("Nested child retainer applies only when enabled"), Corner.A,
				uint8(bRetainRendering ? 0 : 255));
			return true;
		}();

		Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder);
		if (!bBranchSucceeded) return false;
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRetainerBlueprintParametersRenderingTest,
	"Design2UMG.PropertyApplier.RetainerBlueprintParametersRendering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRetainerBlueprintParametersRenderingTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddError(TEXT("Blueprint effect rendering requires real graphics backend"));
		return false;
	}
	const FString Folder = TEXT("/Game/Design2UMG/Automation/RetainerParameters_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
	for (bool bInnerShadow : {false, true})
	{
		FUMGWidgetNode Content;
		Content.Type = TEXT("CanvasPanel");
		Content.Id = TEXT("content");
		Content.Name = TEXT("Content");
		Content.Bounds.W = Content.Bounds.H = ImageSize;
		FUMGWidgetNode Fill;
		Fill.Type = TEXT("Border");
		Fill.Id = TEXT("blue");
		Fill.Name = TEXT("Blue");
		Fill.Bounds.X = Fill.Bounds.Y = bInnerShadow ? 16 : 0;
		Fill.Bounds.W = ImageSize / 2;
		Fill.Bounds.H = bInnerShadow ? ImageSize / 2 : ImageSize;
		Fill.Slot.Offsets.Left = Fill.Bounds.X;
		Fill.Slot.Offsets.Top = Fill.Bounds.Y;
		Fill.Slot.Offsets.Right = Fill.Bounds.W;
		Fill.Slot.Offsets.Bottom = Fill.Bounds.H;
		Fill.Brush.DrawAs = ESlateBrushDrawType::Image;
		Fill.Brush.BrushColor = FLinearColor::Blue;
		Content.Children.Add(Fill);
		if (!bInnerShadow)
		{
			Fill.Id = TEXT("red");
			Fill.Name = TEXT("Red");
			Fill.Bounds.X = ImageSize / 2;
			Fill.Slot.Offsets.Left = Fill.Bounds.X;
			Fill.Brush.BrushColor = FLinearColor::Red;
			Content.Children.Add(Fill);
		}
		TArray<FColor> PlainPixels, EffectPixels;
		for (bool bEffect : {false, true})
		{
			FUMGWidgetData Data;
			Data.WidgetName = FString::Printf(TEXT("WBP_%s_%s"), bInnerShadow ? TEXT("Shadow") : TEXT("Blur"), bEffect ? TEXT("Effect") : TEXT("Plain"));
			Data.RootWidget = Content;
			if (bEffect)
			{
				Data.RootWidget = FUMGWidgetNode();
				Data.RootWidget.Type = TEXT("RetainerBox");
				Data.RootWidget.Id = TEXT("effect");
				Data.RootWidget.Name = TEXT("Effect");
				Data.RootWidget.Bounds.W = Data.RootWidget.Bounds.H = ImageSize;
				Data.RootWidget.Effect.bRetainRendering = true;
				Data.RootWidget.Effect.TextureParameter = TEXT("Texture");
				Data.RootWidget.Effect.EffectMaterial = bInnerShadow ? TEXT("d2u.retainer.inner_shadow") : TEXT("d2u.retainer.foreground_blur");
				Data.RootWidget.Effect.EffectBlurUVStep = FVector2D(0.0325f, 0.0325f);
				if (bInnerShadow)
				{
					Data.RootWidget.Effect.EffectColor = FLinearColor(1.0f, 0.0f, 0.0f, 0.65f);
					Data.RootWidget.Effect.EffectOffset = FVector2D(0.075f, 0.075f);
				}
				Data.RootWidget.Children.Add(Content);
			}
			UWidgetBlueprint* Blueprint = FUMGGenerator::GenerateWidgetBlueprint(Data, Folder);
			if (!TestNotNull(TEXT("Effect fixture blueprint"), Blueprint)
				|| !TestTrue(TEXT("Save effect fixture"), FUMGGenerator::SaveWidgetBlueprint(Blueprint).IsSuccess())) return false;
			Blueprint = DesignBlueprintRenderPreparation::ReloadWidgetBlueprintForRender(Blueprint);
			if (!TestNotNull(TEXT("Reload effect fixture"), Blueprint)) return false;
			TArray<uint8> Png;
			FIntPoint Size;
			FString Error;
			TStrongObjectPtr<UUserWidget> RenderedWidget;
			if (!TestTrue(TEXT("Render saved effect fixture"), FUMGWidgetRenderer::RenderWidgetToPng(Blueprint, Png, Size, &Error, {}, {}, &RenderedWidget)))
			{
				AddError(Error);
				return false;
			}
			if (!TestTrue(TEXT("Decode effect fixture"), ReadRenderedPixels(Png, bEffect ? EffectPixels : PlainPixels))) return false;
			if (bEffect)
			{
				URetainerBox* Retainer = Cast<URetainerBox>(RenderedWidget->WidgetTree->RootWidget);
				const UMaterialInterface* Material = Retainer ? Design2UMGWidgetPropertyAccess::GetRetainerEffectMaterialInterface(*Retainer) : nullptr;
				if (!TestNotNull(TEXT("Saved effect interface"), Material)) return false;
				TestTrue(TEXT("Effect parameters remain on an external saved asset"),
					Material->GetOuter() == Material->GetOutermost()
					&& FPackageName::DoesPackageExist(Material->GetOutermost()->GetName()));
			}
		}
		TestTrue(TEXT("Plain fixture contains opaque blue pixels"), PlainPixels.ContainsByPredicate(
			[](const FColor& Pixel) { return Pixel.B > 200 && Pixel.A == 255; }));
		TestTrue(TEXT("Saved effect changes the rendered pixels"), CountPixelDifferences(PlainPixels, EffectPixels) > 0);
		if (bInnerShadow)
		{
			int32 AlphaDifferences = 0;
			for (int32 Index = 0; Index < PlainPixels.Num(); ++Index)
				if (PlainPixels[Index].A != EffectPixels[Index].A) ++AlphaDifferences;
			TestEqual(TEXT("Saved inner shadow preserves subtree alpha"), AlphaDifferences, 0);
		}
		else
		{
			const FColor Center = EffectPixels[(ImageSize / 2) * ImageSize + ImageSize / 2];
			TestTrue(TEXT("Saved foreground blur mixes the red/blue boundary"), Center.R > 20 && Center.B > 20 && Center.A > 200);
		}
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInnerShadowTransparentBoundaryRenderingTest,
	"Design2UMG.PropertyApplier.RetainerInnerShadowTransparentBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FInnerShadowTransparentBoundaryRenderingTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddError(TEXT("Inner shadow boundary test requires real graphics backend"));
		return false;
	}
	TStrongObjectPtr<UTexture2D> Texture(CreateRetainerSourceTexture());
	if (!TestNotNull(TEXT("Opaque source texture"), Texture.Get())) return false;
	FColor* Source = static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Index = 0; Index < ImageSize * ImageSize; ++Index) Source[Index] = FColor::Blue;
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	UMaterialInterface* Parent = Design2UMGEffectMaterialBuilder::GetOrCreateInnerShadowRetainerMaterial();
	if (!TestNotNull(TEXT("Inner shadow parent"), Parent)) return false;
	TStrongObjectPtr<URetainerBox> Retainer(CreateEffectRetainer(Texture.Get(), Parent, false, false, true));
	if (!TestNotNull(TEXT("Inner shadow retainer"), Retainer.Get())) return false;
	UMaterialInstanceDynamic* Effect = Retainer->GetEffectMaterial();
	if (!TestNotNull(TEXT("Inner shadow dynamic material"), Effect)) return false;
	Effect->SetVectorParameterValue(TEXT("BlurUVStep"), FLinearColor::Transparent);
	Effect->SetVectorParameterValue(TEXT("InnerShadowOffset"), FLinearColor(-2.0f / ImageSize, -2.0f / ImageSize, 0.0f, 0.0f));
	Effect->SetVectorParameterValue(TEXT("InnerShadowColor"), FLinearColor(1.0f, 0.0f, 0.0f, 0.5f));
	Effect->SetScalarParameterValue(TEXT("InnerShadowStrength"), 0.5f);
	TArray<FColor> Pixels;
	if (!TestTrue(TEXT("Inner shadow boundary renders"), DrawEffectRetainer(Retainer.Get(), Pixels))) return false;
	const FColor Center = Pixels[(ImageSize / 2) * ImageSize + ImageSize / 2];
	const FColor Right = Pixels[(ImageSize / 2) * ImageSize + ImageSize - 1];
	const FColor Bottom = Pixels[(ImageSize - 1) * ImageSize + ImageSize / 2];
	TestTrue(TEXT("Opaque interior keeps its blue fill"), Center.B > 200 && Center.R < 10);
	TestTrue(TEXT("Translated cutout casts an inset shadow at the right boundary"), Right.R > Center.R + 30);
	TestTrue(TEXT("Translated cutout casts an inset shadow at the bottom boundary"), Bottom.R > Center.R + 30);
	TestTrue(TEXT("Inset shadow preserves opaque alpha"),
		Pixels.ContainsByPredicate([](const FColor& Pixel) { return Pixel.A != 255; }) == false);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForegroundBlurTransparentBoundaryRenderingTest,
	"Design2UMG.PropertyApplier.RetainerForegroundBlurTransparentBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FForegroundBlurTransparentBoundaryRenderingTest::RunTest(const FString&)
{
	if (GUsingNullRHI)
	{
		AddError(TEXT("Foreground blur boundary test requires real graphics backend"));
		return false;
	}
	TStrongObjectPtr<UTexture2D> Texture(CreateRetainerSourceTexture());
	if (!TestNotNull(TEXT("Opaque blur source texture"), Texture.Get())) return false;
	FColor* Source = static_cast<FColor*>(Texture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Index = 0; Index < ImageSize * ImageSize; ++Index) Source[Index] = FColor::Blue;
	Texture->GetPlatformData()->Mips[0].BulkData.Unlock();
	Texture->UpdateResource();
	UMaterialInterface* Parent = Design2UMGEffectMaterialBuilder::GetOrCreateForegroundBlurRetainerMaterial();
	if (!TestNotNull(TEXT("Foreground blur parent"), Parent)) return false;
	TStrongObjectPtr<URetainerBox> Retainer(CreateEffectRetainer(Texture.Get(), Parent, false, false, false));
	if (!TestNotNull(TEXT("Foreground blur retainer"), Retainer.Get())) return false;
	UMaterialInstanceDynamic* Effect = Retainer->GetEffectMaterial();
	if (!TestNotNull(TEXT("Foreground blur dynamic material"), Effect)) return false;
	Effect->SetVectorParameterValue(TEXT("BlurUVStep"), FLinearColor(2.0f / ImageSize, 2.0f / ImageSize, 0.0f, 0.0f));
	TArray<FColor> Pixels;
	if (!TestTrue(TEXT("Foreground blur boundary renders"), DrawEffectRetainer(Retainer.Get(), Pixels))) return false;
	const FColor Center = Pixels[(ImageSize / 2) * ImageSize + ImageSize / 2];
	TestEqual(TEXT("Opaque interior remains opaque"), Center.A, uint8(255));
	TestTrue(TEXT("Foreground blur fades at the left boundary"), Pixels[(ImageSize / 2) * ImageSize].A < Center.A - 20);
	TestTrue(TEXT("Foreground blur fades at the right boundary"), Pixels[(ImageSize / 2) * ImageSize + ImageSize - 1].A < Center.A - 20);
	TestTrue(TEXT("Foreground blur fades at the top boundary"), Pixels[ImageSize / 2].A < Center.A - 20);
	TestTrue(TEXT("Foreground blur fades at the bottom boundary"), Pixels[(ImageSize - 1) * ImageSize + ImageSize / 2].A < Center.A - 20);
	Effect->SetVectorParameterValue(TEXT("BlurUVStep"), FLinearColor::Transparent);
	if (!TestTrue(TEXT("Zero foreground blur renders"), DrawEffectRetainer(Retainer.Get(), Pixels))) return false;
	TestTrue(TEXT("Zero blur keeps every opaque source pixel opaque"),
		!Pixels.ContainsByPredicate([](const FColor& Pixel) { return Pixel.A != 255; }));
	return true;
}
#endif
