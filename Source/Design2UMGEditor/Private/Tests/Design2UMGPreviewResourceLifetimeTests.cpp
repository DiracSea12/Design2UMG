// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/Build.h"

#if WITH_AUTOMATION_WORKER

#include "Misc/AutomationTest.h"
#include "DesignPreviewMaterializationService.h"
#include "DesignPreviewContracts.h"
#include "Design2UMGSettings.h"
#include "DesignPathRasterizer.h"
#include "PreBuildResourceNormalizer.h"
#include "ProceduralShapeGenerator.h"
#include "PreviewUserWidget.h"
#include "UMGSchema.h"
#include "UMGWidgetTypes.h"
#include "UMGWidgetTreeBuilder.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectIterator.h"

namespace
{
	FUMGWidgetNode MakeGradientImageNode(const FString& Id)
	{
		FUMGWidgetNode Node;
		Node.Type = UMGWidgetTypes::Image;
		Node.Id = Id;
		Node.Name = Id;
		Node.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 96.0f, 96.0f };
		Node.WidgetStyle.WidthOverride = 96.0f;
		Node.WidgetStyle.HeightOverride = 96.0f;
		Node.Brush.Gradient.Type = TEXT("Linear");
		FUMGVisualBrushData::FGradientStopData First;
		First.Position = 0.0f;
		First.Color = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
		FUMGVisualBrushData::FGradientStopData Second;
		Second.Position = 1.0f;
		Second.Color = FLinearColor(0.0f, 0.0f, 1.0f, 1.0f);
		Node.Brush.Gradient.Stops.Add(First);
		Node.Brush.Gradient.Stops.Add(Second);
		return Node;
	}

	FUMGWidgetNode MakePathImageNode(const FString& Id, const FString& Geometry, const float Y)
	{
		FUMGWidgetNode Node;
		Node.Type = UMGWidgetTypes::Image;
		Node.Id = Id;
		Node.Name = Id;
		Node.Bounds = FDesignPixelBounds{0.0f, Y, 200.0f, 200.0f};
		Node.WidgetStyle.WidthOverride = 200.0f;
		Node.WidgetStyle.HeightOverride = 200.0f;
		Node.Path.Geometry = Geometry;
		Node.Path.FillColor = FLinearColor::White;
		return Node;
	}

	/** A non-empty but unbuildable widget type: FUMGWidgetTreeBuilder returns nullptr for it. */
	FUMGWidgetNode MakeUnbuildableNode(const FString& Id)
	{
		FUMGWidgetNode Node;
		Node.Type = TEXT("UnknownPreviewWidgetType");
		Node.Id = Id;
		Node.Name = Id;
		Node.Bounds = FDesignPixelBounds{ 0.0f, 96.0f, 96.0f, 96.0f };
		return Node;
	}

	FUMGWidgetNode MakeCanvasRoot(const FString& Name)
	{
		FUMGWidgetNode Root;
		Root.Type = UMGWidgetTypes::CanvasPanel;
		Root.Id = Name;
		Root.Name = Name;
		Root.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 320.0f, 240.0f };
		return Root;
	}

	FPreviewApprovedSnapshot MakeSnapshot(const FString& WidgetName, const FUMGWidgetNode& Root)
	{
		FPreviewApprovedSnapshot Snapshot;
		Snapshot.RequestId = 11;
		Snapshot.Data.WidgetName = WidgetName;
		Snapshot.Data.RootWidget = Root;
		return Snapshot;
	}

	FDesignPreviewMaterializationRequest MakeRequest(const FPreviewApprovedSnapshot& Snapshot)
	{
		FDesignPreviewMaterializationRequest Request;
		Request.RequestId = Snapshot.RequestId;
		Request.ApprovedSnapshot = Snapshot;
		return Request;
	}

	/** Counts rooted transient textures that were not rooted before the attempt. */
	int32 CountNewRootedTextures(
		const TArray<UTexture2D*>& Before,
		const TArray<UTexture2D*>& After)
	{
		int32 NewCount = 0;
		for (UTexture2D* Texture : After)
		{
			if (Texture && Before.Find(Texture) == INDEX_NONE)
			{
				++NewCount;
			}
		}
		return NewCount;
	}

	UImage* FindFirstImage(UWidget* Root)
	{
		if (!Root)
		{
			return nullptr;
		}
		if (UImage* AsImage = Cast<UImage>(Root))
		{
			return AsImage;
		}
		if (UPanelWidget* AsPanel = Cast<UPanelWidget>(Root))
		{
			for (int32 Index = 0; Index < AsPanel->GetChildrenCount(); ++Index)
			{
				if (UImage* Found = FindFirstImage(AsPanel->GetChildAt(Index)))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	/**
	 * Counts the preview widgets that actually exist in the UObject array. Unlike the
	 * rooted-texture snapshots above, this is what GC is expected to reclaim.
	 */
	int32 CountLivePreviewWidgets()
	{
		int32 Count = 0;
		for (TObjectIterator<UPreviewUserWidget> It; It; ++It)
		{
			if (!It->IsUnreachable())
			{
				++Count;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewResourceLifetimeResetThenGarbageCollectDoesNotAccumulateTest,
	"Design2UMG.Preview.ResourceLifetime.ResetThenGarbageCollectDoesNotAccumulate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewResourceLifetimeResetThenGarbageCollectDoesNotAccumulateTest::RunTest(const FString& Parameters)
{
	// closing a preview has to actually release the preview widget, not just drop a
	// reference. This asserts on the live UObject population after a real garbage
	// collection, so a widget that is merely unrooted but still referenced would fail.
	CollectGarbage(RF_NoFlags);
	const int32 BaseWidgets = CountLivePreviewWidgets();

	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		FUMGWidgetNode Root = MakeCanvasRoot(TEXT("PreviewResources_GcRoot"));
		Root.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));

		FDesignPreviewMaterializationService Service;
		FDesignPreviewMaterializationResult Result;
		TestTrue(TEXT("Preview materializes before the reset"),
			Service.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_Gc"), Root)), Result));
		TestNotNull(TEXT("The materialized widget is alive before the reset"),
			Service.GetMaterializedPreviewWidget());

		Service.Reset();
		TestNull(TEXT("Reset drops the preview widget"), Service.GetMaterializedPreviewWidget());

		CollectGarbage(RF_NoFlags);
		TestEqual(TEXT("Each reset + GC cycle leaves no preview widget behind"),
			CountLivePreviewWidgets(), BaseWidgets);
	}

	// Same guarantee through the destructor path (closing without an explicit Reset).
	for (int32 Cycle = 0; Cycle < 3; ++Cycle)
	{
		FUMGWidgetNode Root = MakeCanvasRoot(TEXT("PreviewResources_GcRoot"));
		Root.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));

		FDesignPreviewMaterializationService Service;
		FDesignPreviewMaterializationResult Result;
		TestTrue(TEXT("Preview materializes before going out of scope"),
			Service.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_Gc"), Root)), Result));
	}

	CollectGarbage(RF_NoFlags);
	TestEqual(TEXT("Six materialized previews leave no preview widget behind after GC"),
		CountLivePreviewWidgets(), BaseWidgets);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewResourceLifetimeFailedAttemptReleasesRootedProceduralTexturesTest,
	"Design2UMG.Preview.ResourceLifetime.FailedAttemptRestoresRootedCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewResourceLifetimeFailedAttemptReleasesRootedProceduralTexturesTest::RunTest(const FString& Parameters)
{
	// Documented sequence: a valid gradient Image is built first, then an
	// unbuildable sibling makes the preview root fail.
	FUMGWidgetNode Root = MakeCanvasRoot(TEXT("PreviewResources_FailRoot"));
	Root.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));
	Root.Children.Add(MakeUnbuildableNode(TEXT("PreviewResources_Unknown")));

	// Expected-message registration must happen before the failure is logged.
	AddExpectedError(TEXT("Failed to create root widget of type"), EAutomationExpectedErrorFlags::Contains, 0);

	const TArray<UTexture2D*> RootedBefore = FProceduralShapeGenerator::GetRootedTextureSnapshot();

	FDesignPreviewMaterializationService Service;
	FDesignPreviewMaterializationResult Result;
	const bool bMaterialized = Service.MaterializePreview(
		MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_Failure"), Root)), Result);

	TestFalse(TEXT("Materialization of a tree with an unbuildable node fails"), bMaterialized);
	TestFalse(TEXT("Failure result does not report success"), Result.bSucceeded);
	TestNull(TEXT("No preview widget survives a failed attempt"), Service.GetMaterializedPreviewWidget());

	const TArray<UTexture2D*> RootedAfter = FProceduralShapeGenerator::GetRootedTextureSnapshot();
	TestEqual(TEXT("No rooted texture survives the failed attempt"),
		CountNewRootedTextures(RootedBefore, RootedAfter), 0);
	TestEqual(TEXT("Rooted texture count returns to the pre-attempt value"),
		RootedAfter.Num(), RootedBefore.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewResourceLifetimeRepeatedFailuresDoNotAccumulateTest,
	"Design2UMG.Preview.ResourceLifetime.RepeatedFailuresDoNotAccumulate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewResourceLifetimeRepeatedFailuresDoNotAccumulateTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Root = MakeCanvasRoot(TEXT("PreviewResources_RepeatRoot"));
	Root.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));
	Root.Children.Add(MakeUnbuildableNode(TEXT("PreviewResources_Unknown")));

	// Expected-message registration must happen before the failures are logged.
	AddExpectedError(TEXT("Failed to create root widget of type"), EAutomationExpectedErrorFlags::Contains, 0);

	const TArray<UTexture2D*> RootedBefore = FProceduralShapeGenerator::GetRootedTextureSnapshot();

	FDesignPreviewMaterializationService Service;
	for (int32 Attempt = 0; Attempt < 3; ++Attempt)
	{
		FDesignPreviewMaterializationResult Result;
		TestFalse(TEXT("Repeated attempt keeps failing"),
			Service.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_Repeat"), Root)), Result));

		const TArray<UTexture2D*> RootedAfter = FProceduralShapeGenerator::GetRootedTextureSnapshot();
		TestEqual(TEXT("Attempt leaves no newly rooted texture"),
			CountNewRootedTextures(RootedBefore, RootedAfter), 0);
	}

	const TArray<UTexture2D*> RootedFinal = FProceduralShapeGenerator::GetRootedTextureSnapshot();
	TestEqual(TEXT("Three failed attempts do not accumulate rooted textures"),
		RootedFinal.Num(), RootedBefore.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewResourceLifetimeSuccessfulPreviewStillMaterializesTest,
	"Design2UMG.Preview.ResourceLifetime.SuccessfulPreviewStillMaterializes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewResourceLifetimeSuccessfulPreviewStillMaterializesTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Root = MakeCanvasRoot(TEXT("PreviewResources_OkRoot"));
	Root.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));

	const TArray<UTexture2D*> RootedBefore = FProceduralShapeGenerator::GetRootedTextureSnapshot();

	FDesignPreviewMaterializationService Service;
	FDesignPreviewMaterializationResult Result;
	TestTrue(TEXT("A valid gradient preview still materializes"),
		Service.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_Success"), Root)), Result));
	TestTrue(TEXT("Successful result reports success"), Result.bSucceeded);

	UUserWidget* PreviewWidget = Service.GetMaterializedPreviewWidget();
	TestNotNull(TEXT("Successful preview keeps a widget"), PreviewWidget);
	if (!PreviewWidget || !PreviewWidget->WidgetTree)
	{
		return false;
	}
	UWidget* PreviewRoot = PreviewWidget->WidgetTree->RootWidget;
	TestNotNull(TEXT("Successful preview has a root widget"), PreviewRoot);
	TestTrue(TEXT("Preview root is the requested CanvasPanel"),
		PreviewRoot != nullptr && PreviewRoot->IsA<UCanvasPanel>());

	UImage* Image = FindFirstImage(PreviewRoot);
	TestNotNull(TEXT("Gradient Image exists in the preview tree"), Image);
	TestTrue(TEXT("Gradient Image keeps its generated texture resource"),
		Image != nullptr && Image->GetBrush().GetResourceObject() != nullptr);

	// On success the widget owns the texture, so this attempt's transient roots are dropped.
	const TArray<UTexture2D*> RootedAfter = FProceduralShapeGenerator::GetRootedTextureSnapshot();
	TestEqual(TEXT("Successful attempt drops its own transient roots"),
		CountNewRootedTextures(RootedBefore, RootedAfter), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewResourceLifetimeReopenAfterFailureDoesNotInheritStateTest,
	"Design2UMG.Preview.ResourceLifetime.ReopenAfterFailureDoesNotInheritState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewResourceLifetimeReopenAfterFailureDoesNotInheritStateTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode FailingRoot = MakeCanvasRoot(TEXT("PreviewResources_FailRoot"));
	FailingRoot.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));
	FailingRoot.Children.Add(MakeUnbuildableNode(TEXT("PreviewResources_Unknown")));

	FUMGWidgetNode GoodRoot = MakeCanvasRoot(TEXT("PreviewResources_OkRoot"));
	GoodRoot.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));

	// Expected-message registration must happen before the failure is logged.
	AddExpectedError(TEXT("Failed to create root widget of type"), EAutomationExpectedErrorFlags::Contains, 0);

	const TArray<UTexture2D*> RootedBefore = FProceduralShapeGenerator::GetRootedTextureSnapshot();

	FDesignPreviewMaterializationService Service;
	FDesignPreviewMaterializationResult FailResult;
	TestFalse(TEXT("First attempt fails"),
		Service.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_First"), FailingRoot)), FailResult));

	Service.Reset();
	TestNull(TEXT("Reset clears the failed preview widget"), Service.GetMaterializedPreviewWidget());

	FDesignPreviewMaterializationResult OkResult;
	TestTrue(TEXT("Reopening with a valid input after a failure succeeds"),
		Service.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_Second"), GoodRoot)), OkResult));
	TestTrue(TEXT("Reopened result reports success"), OkResult.bSucceeded);
	TestEqual(TEXT("Reopened preview carries the new widget name"),
		OkResult.MaterializedWidgetName, FString(TEXT("WBP_PreviewResources_Second")));

	UUserWidget* PreviewWidget = Service.GetMaterializedPreviewWidget();
	TestNotNull(TEXT("Reopened preview has a widget"), PreviewWidget);
	TestTrue(TEXT("Reopened preview does not reuse the failed root"),
		PreviewWidget != nullptr && PreviewWidget->WidgetTree != nullptr
			&& PreviewWidget->WidgetTree->RootWidget != nullptr);

	const TArray<UTexture2D*> RootedAfter = FProceduralShapeGenerator::GetRootedTextureSnapshot();
	TestEqual(TEXT("Failure followed by success leaves no extra rooted textures"),
		RootedAfter.Num(), RootedBefore.Num());
	TestEqual(TEXT("No texture from the failed attempt is still rooted"),
		CountNewRootedTextures(RootedBefore, RootedAfter), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewResourceLifetimeFailureDoesNotDisturbAnotherLivePreviewTest,
	"Design2UMG.Preview.ResourceLifetime.FailureDoesNotDisturbAnotherLivePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewResourceLifetimeFailureDoesNotDisturbAnotherLivePreviewTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode GoodRoot = MakeCanvasRoot(TEXT("PreviewResources_OkRoot"));
	GoodRoot.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));

	FUMGWidgetNode FailingRoot = MakeCanvasRoot(TEXT("PreviewResources_FailRoot"));
	FailingRoot.Children.Add(MakeGradientImageNode(TEXT("PreviewResources_Gradient")));
	FailingRoot.Children.Add(MakeUnbuildableNode(TEXT("PreviewResources_Unknown")));

	// Expected-message registration must happen before the failure is logged.
	AddExpectedError(TEXT("Failed to create root widget of type"), EAutomationExpectedErrorFlags::Contains, 0);

	FDesignPreviewMaterializationService ServiceA;
	FDesignPreviewMaterializationResult ResultA;
	TestTrue(TEXT("Preview A materializes"),
		ServiceA.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_A"), GoodRoot)), ResultA));
	UUserWidget* WidgetA = ServiceA.GetMaterializedPreviewWidget();
	TestNotNull(TEXT("Preview A widget exists"), WidgetA);
	UImage* ImageA = WidgetA && WidgetA->WidgetTree ? FindFirstImage(WidgetA->WidgetTree->RootWidget) : nullptr;
	TestNotNull(TEXT("Preview A gradient Image exists"), ImageA);
	UObject* TextureA = ImageA ? ImageA->GetBrush().GetResourceObject() : nullptr;
	TestNotNull(TEXT("Preview A keeps its generated texture"), TextureA);

	const TArray<UTexture2D*> RootedBeforeB = FProceduralShapeGenerator::GetRootedTextureSnapshot();

	FDesignPreviewMaterializationService ServiceB;
	FDesignPreviewMaterializationResult ResultB;
	TestFalse(TEXT("Preview B fails"),
		ServiceB.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_PreviewResources_B"), FailingRoot)), ResultB));

	const TArray<UTexture2D*> RootedAfterB = FProceduralShapeGenerator::GetRootedTextureSnapshot();
	TestEqual(TEXT("Preview B's failure leaves no newly rooted texture"),
		CountNewRootedTextures(RootedBeforeB, RootedAfterB), 0);

	// Preview A must still be intact and still own its texture.
	TestTrue(TEXT("Preview A widget is still alive"), ServiceA.GetMaterializedPreviewWidget() == WidgetA);
	UWidget* RootWidgetA = (WidgetA && WidgetA->WidgetTree) ? WidgetA->WidgetTree->RootWidget : nullptr;
	TestNotNull(TEXT("Preview A root widget is still set"), RootWidgetA);
	TestTrue(TEXT("Preview A still references the same generated texture"),
		ImageA != nullptr && ImageA->GetBrush().GetResourceObject() == TextureA);
	TestTrue(TEXT("Preview A texture was not marked garbage"),
		TextureA != nullptr && !TextureA->IsUnreachable());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewImageAllocatesOnceTest,
	"Design2UMG.Resources.GeneratedTextureBudget.ImageAllocatesOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewImageAllocatesOnceTest::RunTest(const FString& Parameters)
{
	for (const int32 Mode : { 0, 1, 2 })
	{
		const TArray<UTexture2D*> Before = FProceduralShapeGenerator::GetRootedTextureSnapshot();
		ON_SCOPE_EXIT
		{
			for (UTexture2D* Texture : FProceduralShapeGenerator::GetRootedTextureSnapshot())
			{
				if (!Before.Contains(Texture)) FProceduralShapeGenerator::ReleaseTransientTexture(Texture);
			}
		};
		FUMGWidgetNode Node = MakeGradientImageNode(TEXT("SingleAllocation"));
		TMap<FString, UTexture2D*> Textures;
		if (Mode != 0)
		{
			Node.Brush.Gradient = {};
			Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
			Node.Brush.Image.Mode = TEXT("Crop");
			Node.Brush.Image.Transform = FUMGVisualBrushData::FImageTransformData{ 1, 0.25f, 0, 0, 1, 0 };
		}
		if (Mode == 2)
		{
			UTexture2D* Source = LoadObject<UTexture2D>(nullptr, *Node.Brush.Image.TexturePath.GetValue());
			Node.Brush.Image.TexturePath = TEXT("https://example.invalid/preview-texture");
			Textures.Add(Node.Brush.Image.TexturePath.GetValue(), Source);
		}
		TStrongObjectPtr<UPreviewUserWidget> Owner(NewObject<UPreviewUserWidget>());
		Owner->WidgetTree = NewObject<UWidgetTree>(Owner.Get());
		UImage* Image = Cast<UImage>(FUMGWidgetTreeBuilder::CreateWidgetRecursive(Owner->WidgetTree, Node, nullptr, &Textures));
		TestNotNull(TEXT("Image builds"), Image);
		TArray<UTexture2D*> Created;
		for (UTexture2D* Texture : FProceduralShapeGenerator::GetRootedTextureSnapshot())
		{
			if (!Before.Contains(Texture)) Created.Add(Texture);
		}
		TestEqual(Mode == 0 ? TEXT("Gradient allocates once") : TEXT("Local or preview-map affine crop allocates once"), Created.Num(), 1);
		if (Image && Created.Num() == 1)
		{
			TestEqual(TEXT("The sole allocation is the bound texture"), Image->GetBrush().GetResourceObject(), static_cast<UObject*>(Created[0]));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewBorderActualAllocationBudgetTest,
	"Design2UMG.Resources.GeneratedTextureBudget.PreviewBorderActualAllocations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewBorderActualAllocationBudgetTest::RunTest(const FString& Parameters)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	FUMGWidgetNode Root = MakeCanvasRoot(TEXT("GeneratedTextureBudget_BorderBudgetRoot"));
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FUMGWidgetNode Border = MakeGradientImageNode(
			FString::Printf(TEXT("GeneratedTextureBudget_GradientBorder_%d"), Index));
		Border.Type = UMGWidgetTypes::Border;
		Border.Bounds = FDesignPixelBounds{0.0f, Index * 400.0f, 400.0f, 400.0f};
		Border.WidgetStyle.WidthOverride = 400.0f;
		Border.WidgetStyle.HeightOverride = 400.0f;
		Root.Children.Add(MoveTemp(Border));
	}
	Root.Bounds = FDesignPixelBounds{0.0f, 0.0f, 400.0f, 800.0f};

	FDesignPreviewMaterializationService Service;
	FDesignPreviewMaterializationResult Result;
	TestFalse(TEXT("Two real Border rasters share the preview's one MiB operation budget"),
		Service.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_GeneratedTextureBudget_BorderBudget"), Root)), Result));
	TestFalse(TEXT("The refused preview reports no materialized success"), Result.bSucceeded);
	TestTrue(TEXT("The preview exposes the cumulative allocation refusal"),
		Result.ErrorMessage.Contains(TEXT("operation limit")));
	TestNull(TEXT("A refused preview does not retain a partial widget"),
		Service.GetMaterializedPreviewWidget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewPathBudgetErrorIsVisibleTest,
	"Design2UMG.Resources.GeneratedTextureBudget.PreviewPathBudgetErrorIsVisible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewPathBudgetErrorIsVisibleTest::RunTest(const FString&)
{
	TGuardValue<int32> RestoreBudget(UDesign2UMGSettings::Get()->GeneratedTextureBudgetMiB, 1);
	FDesignPathRasterizer::ClearCache();
	ON_SCOPE_EXIT { FDesignPathRasterizer::ClearCache(); };
	FUMGWidgetNode Root = MakeCanvasRoot(TEXT("GeneratedTextureBudget_PathBudgetRoot"));
	Root.Bounds = FDesignPixelBounds{0.0f, 0.0f, 200.0f, 400.0f};
	Root.Children = {
		MakePathImageNode(TEXT("GeneratedTextureBudget_PathA"), TEXT("M0 0 L10 0 L10 10 L0 10 Z"), 0.0f),
		MakePathImageNode(TEXT("GeneratedTextureBudget_PathB"), TEXT("M0 0 L12 0 L12 10 L0 10 Z"), 200.0f)
	};
	AddExpectedError(
		TEXT("Preview materialization rejected: Total decoded image data exceeds the operation limit"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	FDesignPreviewMaterializationService Service;
	FDesignPreviewMaterializationResult Result;
	TestFalse(TEXT("Two distinct path rasters exceed the preview's configured one-MiB budget"),
		Service.MaterializePreview(MakeRequest(MakeSnapshot(TEXT("WBP_GeneratedTextureBudget_PathBudget"), Root)), Result));
	TestFalse(TEXT("The refused path preview reports no materialized success"), Result.bSucceeded);
	TestTrue(TEXT("The path allocation refusal is visible in the preview result"),
		Result.ErrorMessage.Contains(TEXT("operation limit")));
	TestNull(TEXT("A refused path preview does not retain a partial widget"),
		Service.GetMaterializedPreviewWidget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewOversizedPathFailureReasonTest,
	"Design2UMG.Resources.GeneratedTextureBudget.PreviewOversizedPathFailureReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewOversizedPathFailureReasonTest::RunTest(const FString&)
{
	FUMGWidgetNode VisiblePath = MakePathImageNode(
		TEXT("GeneratedTextureBudget_OversizedPath"), TEXT("M0 0 L10 0 L10 10 L0 10 Z"), 0.0f);
	VisiblePath.WidgetStyle.WidthOverride = static_cast<float>(FDesignPathRasterizer::MaxRasterDimension);
	VisiblePath.WidgetStyle.HeightOverride = static_cast<float>(FDesignPathRasterizer::MaxRasterDimension);
	AddExpectedError(
		TEXT("DesignPathRasterizer: invalid or excessive path texture size"),
		EAutomationExpectedErrorFlags::Contains,
		2);
	AddExpectedError(
		TEXT("Preview materialization rejected: Failed to create generated texture for 'GeneratedTextureBudget_OversizedPath'"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	FString Error;
	TestFalse(TEXT("A visible oversized path is rejected during single-request validation"),
		FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(VisiblePath, Error));
	TestTrue(TEXT("The single-request failure identifies the path and dimension problem"),
		Error.Contains(TEXT("GeneratedTextureBudget_OversizedPath"))
		&& Error.Contains(TEXT("path texture dimensions")));

	FUMGWidgetNode TransparentPath = VisiblePath;
	TransparentPath.Name = TEXT("GeneratedTextureBudget_TransparentOversizedPath");
	TransparentPath.Path.FillColor = FLinearColor::Transparent;
	TestTrue(TEXT("A path with no visible paint remains a legal empty resource"),
		FPreBuildResourceNormalizer::ValidateGeneratedTextureRequests(TransparentPath, Error));
	TestTrue(TEXT("The legal empty path leaves no failure reason"), Error.IsEmpty());

	FUMGWidgetNode Root = MakeCanvasRoot(TEXT("GeneratedTextureBudget_OversizedPathRoot"));
	Root.Children.Add(VisiblePath);
	FDesignPreviewMaterializationService Service;
	FDesignPreviewMaterializationResult Result;
	TestFalse(TEXT("Preview rejects a required path before incomplete widget creation"),
		Service.MaterializePreview(
			MakeRequest(MakeSnapshot(TEXT("WBP_GeneratedTextureBudget_OversizedPath"), Root)), Result));
	TestTrue(TEXT("Preview exposes the oversized path failure reason"),
		Result.ErrorMessage.Contains(TEXT("GeneratedTextureBudget_OversizedPath"))
		&& Result.ErrorMessage.Contains(TEXT("path texture dimensions")));
	TestNull(TEXT("Rejected oversized path preview keeps no partial widget"),
		Service.GetMaterializedPreviewWidget());
	return true;
}

#endif // WITH_AUTOMATION_WORKER
