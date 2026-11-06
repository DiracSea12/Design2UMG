// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGPrototypeAnimationBuilder.h"
#include "UMGTextPaintOwner.h"
#include "UObject/Package.h"
#include "Misc/EngineVersionComparison.h"

#include "Design2UMGLog.h"
#include "Design2UMGPrototypeGraphWiring.h"
#include "Design2UMGAnimationPresets.h"
#include "Design2UMGGeneratedNameSuffixes.h"
#include "DesignWidgetBlueprintUtils.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGWidgetTreeBuilder.h"
#include "NativeTextAnimation.h"
#include "Design2UMGEffectMaterialBuilder.h"
#include "Components/OverlaySlot.h"

#include "Animation/MovieScene2DTransformSection.h"
#include "Animation/MovieScene2DTransformTrack.h"
#include "Animation/MovieSceneMarginSection.h"
#include "Animation/MovieSceneMarginTrack.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/RetainerBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Layout/ArrangedChildren.h"
#include "Widgets/SWidget.h"
#include "UObject/StrongObjectPtr.h"
#include "Tracks/MovieSceneByteTrack.h"
#include "Sections/MovieSceneByteSection.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Sections/MovieSceneColorSection.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "WidgetBlueprint.h"

namespace UMGPrototypeAnimationBuilder
{

constexpr TCHAR PrototypeAnimationPrefix[] = TEXT("__d2u_nav_anim_");

namespace
{
	const FUMGWidgetNode* FindWidgetNodeById(const FUMGWidgetNode& Node, const FString& TargetId)
	{
		if (Node.Id == TargetId)
		{
			return &Node;
		}

		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Found = FindWidgetNodeById(Child, TargetId))
			{
				return Found;
			}
		}
		return nullptr;
	}

	FVector2D ResolvePrototypeAnimationOffset(const FDesignPixelBounds& Bounds, const FString& Direction)
	{
		if (Direction == TEXT("LEFT")) return FVector2D(-Bounds.W, 0);
		if (Direction == TEXT("RIGHT")) return FVector2D(Bounds.W, 0);
		if (Direction == TEXT("TOP")) return FVector2D(0, -Bounds.H);
		if (Direction == TEXT("BOTTOM")) return FVector2D(0, Bounds.H);
		return FVector2D::ZeroVector;
	}

	FName MakePrototypeAnimationName(const int32 BindingIndex, const TCHAR* Phase)
	{
		return FName(*FString::Printf(TEXT("%s%03d_%s"), PrototypeAnimationPrefix, BindingIndex, Phase));
	}

	FFrameNumber PrototypeSecondsToFrame(const UMovieScene* MovieScene, const float Seconds)
	{
		const FFrameRate TickResolution = MovieScene ? MovieScene->GetTickResolution() : FFrameRate(24000, 1);
		return FFrameNumber(static_cast<int32>(FMath::RoundToInt64(Seconds * TickResolution.AsDecimal())));
	}

	struct FAnimationWidget
	{
		UWidget* Widget = nullptr;
		const FUMGWidgetNode* Node = nullptr;
	};

	void VisitSourceWidgets(const FUMGWidgetNode& Node, UWidget* Widget,
		TFunctionRef<void(const FUMGWidgetNode&, UWidget*)> Visitor)
	{
		if (!Widget || !FUMGWidgetTreeBuilder::MatchesWidgetBaseName(Widget->GetName(),
			FUMGWidgetTreeBuilder::SanitizeWidgetName(Node.Name, Node.Type).ToString())) return;
		Visitor(Node, Widget);
		auto* Panel = Cast<UPanelWidget>(Widget);
		if (!Panel || Panel->GetChildrenCount() != Node.Children.Num()) return;
		for (int32 ChildIndex = 0; ChildIndex < Node.Children.Num(); ++ChildIndex)
			VisitSourceWidgets(Node.Children[ChildIndex], Panel->GetChildAt(ChildIndex), Visitor);
	}

	/** Source identities resolved once through the generated native tree; an ambiguous identity resolves to nothing. */
	class FSourceWidgets
	{
	public:
		FSourceWidgets(const FUMGWidgetNode& Root, UWidget* NativeRoot)
		{
			VisitSourceWidgets(Root, NativeRoot, [&](const FUMGWidgetNode& Node, UWidget* Widget)
			{
				if (FAnimationWidget* Existing = Widgets.Find(Node.Id)) *Existing = {};
				else if (!Node.Id.IsEmpty()) Widgets.Add(Node.Id, {Widget, &Node});
			});
		}

		FAnimationWidget Find(const FString& Id) const { return Widgets.FindRef(Id); }

	private:
		TMap<FString, FAnimationWidget> Widgets;
	};

	using FNativeGeometries = TMap<const SWidget*, FGeometry>;

	void CollectNativeLayoutGeometry(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, FNativeGeometries& Geometries)
	{
		Geometries.Add(&Widget.Get(), Geometry);
		FArrangedChildren Children(EVisibility::Visible);
		Widget->ArrangeChildren(Geometry, Children);
		for (int32 Index = 0; Index < Children.Num(); ++Index)
			CollectNativeLayoutGeometry(Children[Index].Widget, Children[Index].Geometry, Geometries);
	}

	struct FVisibilityOverride
	{
		UWidget* Widget;
		ESlateVisibility Visibility;
	};

	/**
	 * One compiled native instance arranged at the design viewport. Generated tracks only edit collapsed
	 * presentation layers, so one compilation serves every transition measured before the next compile.
	 */
	class FNativeLayoutProbe
	{
	public:
		FNativeLayoutProbe(UWidgetBlueprint* InBlueprint, const FUMGWidgetNode& Root)
			: Blueprint(InBlueprint), Viewport(Root.Bounds.W, Root.Bounds.H) {}
		~FNativeLayoutProbe() { if (Instance) Instance->ReleaseSlateResources(true); }

		bool Initialize(FString& Error)
		{
			if (Instance) return true;
			if (Viewport.X <= 0 || Viewport.Y <= 0) { Error = TEXT("native layout measurement requires a positive design viewport"); return false; }
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			if (Blueprint->Status == BS_Error) { Error = TEXT("native layout measurement blueprint did not compile"); return false; }
			Instance.Reset(NewObject<UUserWidget>(GetTransientPackage(), Blueprint->GeneratedClass));
			Instance->SetDesignerFlags(EWidgetDesignFlags::Designing | EWidgetDesignFlags::ExecutePreConstruct);
			if (!Instance->Initialize()) { Instance.Reset(); Error = TEXT("native layout measurement instance did not initialize"); return false; }
			Slate = Instance->TakeWidget();
			Target.Reset(FWidgetRenderer::CreateTargetFor(Viewport, TF_Nearest, true));
			return true;
		}

		UWidget* Live(const UWidget* Design) const
		{
			return Design ? Instance->WidgetTree->FindWidget(Design->GetFName()) : nullptr;
		}

		/** Arranges under temporary visibility, keeping visible children that an ancestor clip culls from paint. */
		FNativeGeometries Arrange(TConstArrayView<FVisibilityOverride> Overrides)
		{
			TArray<FVisibilityOverride> Previous;
			for (const auto& Override : Overrides)
			{
				Previous.Add({Override.Widget, Override.Widget->GetVisibility()});
				Override.Widget->SetVisibility(Override.Visibility);
			}
			Instance->ForceLayoutPrepass();
			Renderer.DrawWidget(Target.Get(), Slate.ToSharedRef(), 1.0f, Viewport, 0.0f);
			FlushRenderingCommands();
			FNativeGeometries Geometries;
			CollectNativeLayoutGeometry(Slate.ToSharedRef(), Slate->GetCachedGeometry(), Geometries);
			for (int32 Index = Previous.Num() - 1; Index >= 0; --Index) Previous[Index].Widget->SetVisibility(Previous[Index].Visibility);
			return Geometries;
		}

		static const FGeometry* Geometry(const UWidget* LiveWidget, const FNativeGeometries& Geometries)
		{
			const TSharedPtr<SWidget> Widget = LiveWidget ? LiveWidget->GetCachedWidget() : TSharedPtr<SWidget>();
			return Widget ? Geometries.Find(Widget.Get()) : nullptr;
		}

	private:
		UWidgetBlueprint* Blueprint;
		FVector2D Viewport;
		TStrongObjectPtr<UUserWidget> Instance;
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		TSharedPtr<SWidget> Slate;
		FWidgetRenderer Renderer{false};
	};

	/** The nearest clipping ancestor's geometry, when every widget up to it is translation-only and not animated. */
	const FGeometry* FindClipDomain(UWidget* Live, const FNativeGeometries& Geometries, TFunctionRef<bool(const UWidget*)> IsAnimated)
	{
		for (UWidget* Current = Live; Current; Current = Current->GetParent())
		{
			const FWidgetTransform Transform = Current->GetRenderTransform();
			if (!Transform.Scale.Equals(FVector2D(1, 1)) || !Transform.Shear.IsNearlyZero() || !FMath::IsNearlyZero(Transform.Angle)
				|| IsAnimated(Current)) return nullptr;
			if (Current->GetClipping() == EWidgetClipping::ClipToBounds || Current->GetClipping() == EWidgetClipping::ClipToBoundsAlways)
				return FNativeLayoutProbe::Geometry(Current, Geometries);
		}
		return nullptr;
	}
	FString ColorProperty(const FAnimationWidget& Target)
	{
		const UWidget* Widget = Target.Widget;
		const auto& Node = *Target.Node;
		if (Node.Brush.Gradient.HasAny() || Node.Brush.Image.HasAny() || Node.Brush.Shader.IsSet() || Node.Path.HasPaintGeometry()) return FString();
		if (Widget->GetClass() == UImage::StaticClass())
		{
			return Node.Brush.BrushColor.IsSet() ? TEXT("Brush.TintColor") : FString();
		}
		if (Widget->GetClass() == UBorder::StaticClass())
		{
			return Node.Brush.BrushColor.IsSet() ? TEXT("BrushColor") : FString();
		}
		if (Widget->GetClass() == UTextBlock::StaticClass() && Node.Text.Color.IsSet()) return TEXT("ColorAndOpacity");
		return FString();
	}

	FAnimationWidget ColorOwner(const FAnimationWidget& Target)
	{
		if (Target.Node->Type == TEXT("Overlay") && !Target.Node->Children.IsEmpty()
			&& Target.Node->Children[0].Id == Target.Node->Id + TEXT("__layout_content"))
			return ColorOwner({CastChecked<UPanelWidget>(Target.Widget)->GetChildAt(0), &Target.Node->Children[0]});
		if (Target.Node->SourceVisualChildIndex != INDEX_NONE)
		{
			const int32 Index = Target.Node->SourceVisualChildIndex;
			return ColorOwner({CastChecked<UPanelWidget>(Target.Widget)->GetChildAt(Index), &Target.Node->Children[Index]});
		}
		const auto* Button = Cast<UButton>(Target.Widget);
		if (Button && Target.Node->Children.Num() == 1 && Button->GetChildrenCount() == 1
			&& Target.Node->Children[0].Id == Target.Node->Id + Design2UMGGeneratedNameSuffixes::Content())
			return ColorOwner({Button->GetChildAt(0), &Target.Node->Children[0]});
		return Target;
	}

	FAnimationWidget TextPaintOwner(const FAnimationWidget& Target)
	{
		const auto* Owner = UMGTextPaintOwner::Find(*Target.Node);
		FAnimationWidget Result{};
		if (Owner) VisitSourceWidgets(*Target.Node, Target.Widget,
			[&](const FUMGWidgetNode& Node, UWidget* Widget)
			{ if (&Node == Owner) Result = {Widget, &Node}; });
		return Result;
	}

	UTextBlock* TextOwner(const FAnimationWidget& Target)
	{
		FAnimationWidget Glyph = Target;
		while (Glyph.Widget && Glyph.Node)
		{
			Glyph = ColorOwner(Glyph);
			if (Glyph.Node->Type == TEXT("TextBlock")) return Cast<UTextBlock>(Glyph.Widget);
			const auto Paint = TextPaintOwner(Glyph);
			if (Paint.Widget) Glyph = Paint;
			const bool bContentContainer = (Glyph.Node->Type == TEXT("SizeBox") && Glyph.Widget->IsA<USizeBox>())
				|| (Glyph.Node->Type == TEXT("ScaleBox") && Glyph.Widget->IsA<UScaleBox>())
				|| (Glyph.Node->Type == TEXT("Border") && Glyph.Widget->IsA<UBorder>())
				|| (Glyph.Node->Type == TEXT("Button") && Glyph.Widget->IsA<UButton>())
				|| (Glyph.Node->Type == TEXT("RetainerBox") && Glyph.Widget->IsA<URetainerBox>());
			auto* Panel = Cast<UPanelWidget>(Glyph.Widget);
			if (!bContentContainer || !Panel || Glyph.Node->Children.Num() != 1 || Panel->GetChildrenCount() != 1) return nullptr;
			Glyph = {Panel->GetChildAt(0), &Glyph.Node->Children[0]};
		}
		return nullptr;
	}

	FAnimationWidget SurfaceOwner(const FAnimationWidget& Target)
	{
		FAnimationWidget Visual = ColorOwner(Target);
		if (Visual.Node->Effect.SurfacePaints.IsSet()) return Visual;
		if (!Visual.Node->Children.IsEmpty() && Visual.Node->Children[0].Effect.SurfacePaints.IsSet())
			return {CastChecked<UPanelWidget>(Visual.Widget)->GetChildAt(0), &Visual.Node->Children[0]};
		return {};
	}

	UMovieSceneMarginSection* AddSlotTrack(UWidgetAnimation* Animation, UWidget* Widget, FName Property, const FString& Path, const FMargin& Start, const FMargin& End, FChannelWriter Keys)
	{
		UPanelSlot* Slot = Widget->Slot;
		const FGuid Guid = Animation->MovieScene->AddPossessable(Widget->GetName() + TEXT(" Slot"), Slot->GetClass());
		FWidgetAnimationBinding Binding;
		Binding.AnimationGuid = Guid; Binding.WidgetName = Widget->GetFName(); Binding.SlotWidgetName = Slot->Parent->GetFName();
		Animation->AnimationBindings.Add(Binding);
		auto* Track = Animation->MovieScene->AddTrack<UMovieSceneMarginTrack>(Guid);
		Track->SetPropertyNameAndPath(Property, Path);
		auto* Section = CastChecked<UMovieSceneMarginSection>(Track->CreateNewSection());
		Section->SetRange(Animation->MovieScene->GetPlaybackRange()); Section->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
		Keys(Section->LeftCurve, Start.Left, End.Left); Keys(Section->TopCurve, Start.Top, End.Top);
		Keys(Section->RightCurve, Start.Right, End.Right); Keys(Section->BottomCurve, Start.Bottom, End.Bottom);
		Track->AddSection(*Section);
		return Section;
	}

	struct FVisibilityKey
	{
		FFrameNumber Frame;
		ESlateVisibility Visibility;
	};

	void AddVisibilityTrack(UWidgetAnimation* Animation, const FGuid& Binding, TConstArrayView<FVisibilityKey> Keys)
	{
		auto* Track = Animation->MovieScene->AddTrack<UMovieSceneByteTrack>(Binding);
		Track->SetPropertyNameAndPath(TEXT("Visibility"), TEXT("Visibility"));
		auto* Section = CastChecked<UMovieSceneByteSection>(Track->CreateNewSection());
		Section->SetRange(Animation->MovieScene->GetPlaybackRange()); Section->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
		for (const FVisibilityKey& Key : Keys) Section->ByteCurve.GetData().AddKey(Key.Frame, static_cast<uint8>(Key.Visibility));
		Track->AddSection(*Section);
	}

	/** Images tint through the brush; other solid-color owners animate their own color property. */
	FName ColorPropertyName(const UWidget* Widget, const FString& Path)
	{
		return Widget->GetClass() == UImage::StaticClass() ? FName(TEXT("BrushTintColor")) : FName(*Path);
	}

	int32 PaintTargetIndex(const TOptional<TArray<FUMGSurfacePaintTarget>>& Targets, const FString& DestinationId)
	{
		return Targets.IsSet() ? Targets->IndexOfByPredicate([&](const FUMGSurfacePaintTarget& Item)
			{ return Item.DestinationWidgetId == DestinationId; }) : INDEX_NONE;
	}

	/** The paint-progress track of a text's native retainer toward one destination paint. */
	bool AddTextPaintTrack(NativeTextAnimation::FRefreshBuilder& Refresh, UWidgetAnimation* Animation, const FAnimationWidget& Text,
		const FString& DestinationId, FChannelWriter Keys, FString& Error)
	{
		const FAnimationWidget Paint = TextPaintOwner(Text);
		auto* Retainer = Cast<URetainerBox>(Paint.Widget);
		const int32 Index = Paint.Node ? PaintTargetIndex(Paint.Node->Effect.TextPaintTargets, DestinationId) : INDEX_NONE;
		if (!Retainer || Index == INDEX_NONE)
		{
			Error = FString::Printf(TEXT("text paint toward '%s' has no unique native retainer and destination paint"), *DestinationId);
			return false;
		}
		return Refresh.AddPaintTrack(Animation, Retainer, Index, Keys, Error);
	}

	/** Extends capture bounds by an endpoint's blurred box shadows, with the paint origin ranging over [Low, High]. */
	void IncludeShadowPaint(FBox2D& Bounds, const FUMGSharedCaptureEndpoint& Endpoint, const FVector2D& Low, const FVector2D& High)
	{
		const FVector4& Paint = Endpoint.CaptureBounds;
		Bounds += FVector2D(Low.X + Paint.X, Low.Y + Paint.Y);
		Bounds += FVector2D(High.X + Paint.X + Paint.Z, High.Y + Paint.Y + Paint.W);
	}

	struct FSharedCaptureFrame
	{
		UWidget* Capture = nullptr;
		UWidget* Shadow = nullptr;
		UWidget* Content = nullptr;
		FVector2D SourceSize = FVector2D::ZeroVector;
		FVector2D Pivot = FVector2D::ZeroVector;
		FBox2D Bounds = FBox2D(ForceInit);
		TArray<UMovieSceneMarginSection*> CaptureSections;
		TArray<UMovieSceneMarginSection*> ContentSections;
		TArray<FUMGPrototypeTransitionData*> Plans;
	};
	using FSharedCaptureFrames = TMap<UWidget*, FSharedCaptureFrame>;

	FVector2D WrittenChannelBounds(const FMovieSceneFloatChannel& Channel)
	{
		const auto Data = Channel.GetData();
		const auto Times = Data.GetTimes(); const auto Values = Data.GetValues();
		double Low = Values[0].Value, High = Low;
		const auto Add = [&](double Value) { Low = FMath::Min(Low, Value); High = FMath::Max(High, Value); };
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			Add(Values[Index].Value);
			if (Index + 1 == Values.Num() || Values[Index].InterpMode != RCIM_Cubic) continue;
			const auto& From = Values[Index]; const auto& To = Values[Index+1];
			const double Frames = Times[Index+1].Value - Times[Index].Value;
			const double Rate = Channel.GetTickResolution().AsDecimal();
			const auto Weighted = [](ERichCurveTangentWeightMode Mode, bool bLeave)
			{ return Mode == RCTWM_WeightedBoth || Mode == (bLeave ? RCTWM_WeightedLeave : RCTWM_WeightedArrive); };
			const double Leave = Weighted(From.Tangent.TangentWeightMode, true)
				? FMath::Sin(FMath::Atan(From.Tangent.LeaveTangent * Rate)) * From.Tangent.LeaveTangentWeight
				: From.Tangent.LeaveTangent * Frames / 3;
			const double Arrive = Weighted(To.Tangent.TangentWeightMode, false)
				? FMath::Sin(FMath::Atan(To.Tangent.ArriveTangent * Rate)) * To.Tangent.ArriveTangentWeight
				: To.Tangent.ArriveTangent * Frames / 3;
			Add(From.Value + Leave); Add(To.Value - Arrive);
		}
		return FVector2D(Low, High);
	}

	void ShiftChannel(FMovieSceneFloatChannel& Channel, float Offset)
	{
		for (auto& Value : Channel.GetData().GetValues()) Value.Value += Offset;
	}

	bool RegisterStaticSharedCaptures(const FUMGWidgetNode& Root, UWidget* NativeRoot, FNativeLayoutProbe& Probe,
		FSharedCaptureFrames& Frames, FString& Error)
	{
		// The owner-local rectangle of the nearest stable clip, measured with the owner's hidden ancestry shown.
		const auto MeasureStableDomain = [&](UWidget* Owner, FBox2D& Bounds) -> bool
		{
			if (!Probe.Initialize(Error)) return false;
			UWidget* Live = Probe.Live(Owner);
			if (!Live) { Error = TEXT("static shared capture owner is absent from its native measurement instance"); return false; }
			TArray<FVisibilityOverride> Shown;
			for (UWidget* Current = Live; Current; Current = Current->GetParent())
				if (Current->GetVisibility() == ESlateVisibility::Collapsed || Current->GetVisibility() == ESlateVisibility::Hidden)
					Shown.Add({Current, ESlateVisibility::Visible});
			const FNativeGeometries Geometries = Probe.Arrange(Shown);
			const FGeometry* OwnerGeometry = FNativeLayoutProbe::Geometry(Live, Geometries);
			const FGeometry* Clip = FindClipDomain(Live, Geometries, [](const UWidget*) { return false; });
			if (!OwnerGeometry || !Clip) { Error = TEXT("static shared owner has no arranged translation-only native clipping domain"); return false; }
			const FVector2D Size(Clip->GetLocalSize());
			if (Size.X <= 0 || Size.Y <= 0) { Error = TEXT("static shared capture native clipping domain has no positive measured geometry"); return false; }
			Bounds = FBox2D(FVector2D(OwnerGeometry->AbsoluteToLocal(Clip->LocalToAbsolute(FVector2D::ZeroVector))),
				FVector2D(OwnerGeometry->AbsoluteToLocal(Clip->LocalToAbsolute(Size))));
			return true;
		};
		TMap<UWidget*, const FUMGWidgetNode*> Models;
		VisitSourceWidgets(Root, NativeRoot, [&](const FUMGWidgetNode& Node, UWidget* Widget) { Models.Add(Widget, &Node); });
		for (const auto& Pair : Models)
		{
			UWidget* Widget = Pair.Key;
			const auto& Node = *Pair.Value;
			if (Node.Type != TEXT("RetainerBox") || !Node.Effect.SharedCapture.IsSet()
				|| Node.Effect.SharedCapture->bSubtreeShadow || Frames.Contains(Widget)) continue;
			const auto& Endpoint = Node.Effect.SharedCapture->Source;
			auto* Parent = Cast<UPanelWidget>(Widget->GetParent());
			const auto* ParentNode = Models.FindRef(Parent);
			if (!Parent || !ParentNode || !Cast<UOverlaySlot>(Widget->Slot))
			{ Error = FString::Printf(TEXT("static shared capture '%s' (native '%s') has no native owner slot"), *Node.Id, *Widget->GetPathName()); return false; }
			FBox2D Bounds(FVector2D::ZeroVector, Endpoint.Size);
			if (!Endpoint.bClipContent && !MeasureStableDomain(Parent, Bounds))
			{ Error = FString::Printf(TEXT("static shared owner '%s' (native '%s'): %s"), *Node.Id, *Widget->GetPathName(), *Error); return false; }
			auto& Frame = Frames.FindOrAdd(Widget);
			Frame.Capture = Widget; Frame.SourceSize = Endpoint.Size; Frame.Pivot = Parent->GetRenderTransformPivot();
			Frame.Content = CastChecked<UPanelWidget>(CastChecked<URetainerBox>(Widget)->GetContent())->GetChildAt(0);
			Frame.Bounds = Bounds;
			UWidget* Shadow = Parent->GetParent() ? Parent->GetParent()->GetParent() : nullptr;
			const auto* ShadowNode = Models.FindRef(Shadow);
			if (ShadowNode && ShadowNode->Effect.SharedCapture.IsSet() && ShadowNode->Effect.SharedCapture->bSubtreeShadow)
			{
				Frame.Shadow = Shadow;
				Frame.Pivot = Shadow->GetParent()->GetRenderTransformPivot();
				IncludeShadowPaint(Frame.Bounds, ShadowNode->Effect.SharedCapture->Source, FVector2D::ZeroVector, FVector2D::ZeroVector);
			}
		}
		return true;
	}
	bool FinalizeSharedCaptureFrames(FSharedCaptureFrames& Frames, FString& Error)
	{
		for (auto& Pair : Frames)
		{
			auto& Frame = Pair.Value;
			Frame.Bounds.Min.X = FMath::FloorToDouble(Frame.Bounds.Min.X); Frame.Bounds.Min.Y = FMath::FloorToDouble(Frame.Bounds.Min.Y);
			Frame.Bounds.Max.X = FMath::CeilToDouble(Frame.Bounds.Max.X); Frame.Bounds.Max.Y = FMath::CeilToDouble(Frame.Bounds.Max.Y);
			const FVector2D Origin = Frame.Bounds.Min, Size = Frame.Bounds.GetSize();
			const FVector4 Rect(Origin.X, Origin.Y, Size.X, Size.Y);
			if (!Design2UMGEffectMaterialBuilder::FinalizeSharedCaptureFrame(Frame.Capture, Rect, Frame.Pivot, Error)
				|| (Frame.Shadow && !Design2UMGEffectMaterialBuilder::FinalizeSharedCaptureFrame(Frame.Shadow, Rect, Frame.Pivot, Error)))
			{
				Error = FString::Printf(TEXT("capture '%s', frame=(%g,%g,%g,%g): %s"),
					*GetPathNameSafe(Frame.Capture), Rect.X, Rect.Y, Rect.Z, Rect.W, *Error);
				return false;
			}
			const FMargin Padding(Origin.X, Origin.Y, Frame.SourceSize.X - Frame.Bounds.Max.X, Frame.SourceSize.Y - Frame.Bounds.Max.Y);
			CastChecked<UOverlaySlot>(Frame.Capture->Slot)->SetPadding(Padding);
			if (Frame.Shadow)
			{
				CastChecked<UOverlaySlot>(Frame.Shadow->Slot)->SetPadding(Padding);
				auto* Paints = CastChecked<UPanelWidget>(CastChecked<URetainerBox>(Frame.Shadow)->GetContent())->GetChildAt(0);
				auto* PaintSlot = CastChecked<UCanvasPanelSlot>(Paints->Slot);
				PaintSlot->SetAutoSize(false); PaintSlot->SetAnchors(FAnchors(0)); PaintSlot->SetAlignment(FVector2D::ZeroVector);
				PaintSlot->SetOffsets(FMargin(-Origin.X, -Origin.Y, Frame.SourceSize.X, Frame.SourceSize.Y));
			}
			auto* ContentSlot = CastChecked<UCanvasPanelSlot>(Frame.Content->Slot);
			ContentSlot->SetAutoSize(false); ContentSlot->SetAnchors(FAnchors(0)); ContentSlot->SetAlignment(FVector2D::ZeroVector);
			ContentSlot->SetOffsets(FMargin(-Origin.X, -Origin.Y, Frame.SourceSize.X, Frame.SourceSize.Y));
			for (auto* Section : Frame.CaptureSections)
			{
				ShiftChannel(Section->LeftCurve, Origin.X); ShiftChannel(Section->TopCurve, Origin.Y);
				ShiftChannel(Section->RightCurve, -Frame.Bounds.Max.X); ShiftChannel(Section->BottomCurve, -Frame.Bounds.Max.Y);
			}
			for (auto* Section : Frame.ContentSections)
			{ ShiftChannel(Section->LeftCurve, -Origin.X); ShiftChannel(Section->TopCurve, -Origin.Y); }
		}
		return true;
	}

	struct FLayoutBox
	{
		FVector2D Position;
		FVector2D Size;
	};

	FLayoutBox LayoutBox(const FGeometry& Geometry)
	{
		return {FVector2D(Geometry.GetAccumulatedLayoutTransform().GetTranslation()), FVector2D(Geometry.GetLocalSize())};
	}

	/** Native layout and property tracks of one fixed-viewport transition, played on its collapsed presentation layer. */
	class FFixedLayoutAnimation
	{
	public:
		FFixedLayoutAnimation(NativeTextAnimation::FRefreshBuilder& InRefresh, const FSourceWidgets& InWidgets, FNativeLayoutProbe& InProbe,
			FUMGPrototypeTransitionData& InPlan, UWidgetAnimation* InAnimation, bool bInTemporary, FChannelWriter InKeys)
			: Refresh(InRefresh), Widgets(InWidgets), Probe(InProbe), Plan(InPlan), Animation(InAnimation)
			, bTemporary(bInTemporary), Keys(InKeys) {}

		bool Build(FSharedCaptureFrames& SharedFrames)
		{
			if (!MeasureEndpoints() || !ResolvePresentationPoses() || !AddLayoutTargetTracks() || !AddSharedCaptureTracks()) return false;
			AddPresentationTracks();
			for (auto& Pair : PendingFrames)
			{
				auto& Frame = SharedFrames.FindOrAdd(Pair.Key);
				if (!Frame.Capture) { Frame = MoveTemp(Pair.Value); continue; }
				Frame.Bounds += Pair.Value.Bounds;
				Frame.CaptureSections.Append(Pair.Value.CaptureSections); Frame.ContentSections.Append(Pair.Value.ContentSections);
				for (auto* UsedPlan : Pair.Value.Plans) Frame.Plans.AddUnique(UsedPlan);
			}
			return true;
		}

	private:
		FAnimationWidget Find(const FString& Id) const { return Widgets.Find(Id); }

		bool Fail(const FString& Diagnostic)
		{
			Plan.Diagnostic = Diagnostic;
			return false;
		}

		const FUMGPrototypeWidgetAnimationTarget* ChangeOf(const FString& NodeId) const
		{
			return Plan.WidgetTargets.FindByPredicate([&](const FUMGPrototypeWidgetAnimationTarget& Item) { return Item.NodeId == NodeId; });
		}

		bool MeasureEndpoints()
		{
			Source = Find(Plan.LayoutSourceWidgetId); Destination = Find(Plan.LayoutDestinationWidgetId);
			Original = Find(Plan.LayoutOriginalWidgetId); Presentation = Find(Plan.LayoutPresentationWidgetId);
			const FAnimationWidget Page = Find(Plan.LayoutSourcePageId);
			if (!Source.Widget || !Destination.Widget || !Original.Widget || !Cast<USizeBox>(Presentation.Widget) || !Page.Widget)
				return Fail(TEXT("fixed viewport animation has no complete native endpoints and presentation"));
			if (!Probe.Initialize(Plan.Diagnostic)) return false;
			UWidget* LiveSource = Probe.Live(Source.Widget);
			UWidget* LiveDestination = Probe.Live(Destination.Widget);
			UWidget* LivePage = Probe.Live(Page.Widget);
			if (!LiveSource || !LiveDestination || !LivePage) return Fail(TEXT("compiled layout endpoints are absent from the native instance"));
			StartGeometry = Probe.Arrange({{LivePage, ESlateVisibility::Visible}, {LiveSource, ESlateVisibility::Visible},
				{LiveDestination, ESlateVisibility::Collapsed}});
			EndGeometry = Probe.Arrange({{LivePage, ESlateVisibility::Visible}, {LiveSource, ESlateVisibility::Collapsed},
				{LiveDestination, ESlateVisibility::Visible}});
			CollectBoxes(false, StartGeometry, StartBoxes);
			CollectBoxes(true, EndGeometry, EndBoxes);
			const FLayoutBox* StartSource = StartBoxes.Find(Source.Widget);
			const FLayoutBox* EndDestination = EndBoxes.Find(Destination.Widget);
			if (!StartSource || !EndDestination || StartSource->Size.X <= 0 || StartSource->Size.Y <= 0
				|| EndDestination->Size.X <= 0 || EndDestination->Size.Y <= 0)
				return Fail(TEXT("native endpoint layout did not produce positive root geometry"));
			StartRoot = *StartSource; EndRoot = *EndDestination;
			return true;
		}

		void CollectBoxes(bool bEnd, const FNativeGeometries& Geometries, TMap<UWidget*, FLayoutBox>& Boxes) const
		{
			for (const auto& Mapping : Plan.LayoutTargets)
			{
				const auto* Change = ChangeOf(Mapping.NodeId);
				if (Change && !(bEnd ? Change->bEndPresent : Change->bStartPresent)) continue;
				const FAnimationWidget Item = Find(bEnd ? Mapping.DestinationWidgetId : Mapping.NodeId);
				if (const FGeometry* Geometry = FNativeLayoutProbe::Geometry(Probe.Live(Item.Widget), Geometries))
					Boxes.Add(Item.Widget, LayoutBox(*Geometry));
			}
		}

		/**
		 * Presentation poses at both ends. A layer measured at only one end (one-sided presence, or a synthetic wrapper
		 * without a destination) keeps that native pose relative to its moving parent, so parents resolve first.
		 */
		bool ResolvePresentationPoses()
		{
			TArray<UWidget*> Pending;
			for (const auto& Mapping : Plan.LayoutTargets)
			{
				const FAnimationWidget Painted = Find(Mapping.PresentationWidgetId);
				const auto* Presence = ChangeOf(Mapping.NodeId);
				const FLayoutBox* A = !Presence || Presence->bStartPresent ? StartBoxes.Find(Find(Mapping.NodeId).Widget) : nullptr;
				const FLayoutBox* B = !Presence || Presence->bEndPresent ? EndBoxes.Find(Find(Mapping.DestinationWidgetId).Widget) : nullptr;
				if (!Painted.Widget || (!A && !(Presence && !Presence->bStartPresent && B)))
					return Fail(FString::Printf(TEXT("fixed presentation '%s' -> '%s' has no measured native endpoint"),
						*Mapping.NodeId, *Mapping.DestinationWidgetId));
				if (A) Starts.Add(Painted.Widget, *A);
				if (B) Ends.Add(Painted.Widget, *B);
				if (!A || !B) Pending.Add(Painted.Widget);
			}
			const auto PosedParent = [&](const UWidget* Widget)
			{
				UWidget* Parent = Widget->GetParent();
				while (Parent && !Starts.Contains(Parent) && !Pending.Contains(Parent)) Parent = Parent->GetParent();
				return Parent;
			};
			while (!Pending.IsEmpty())
			{
				for (int32 Index = Pending.Num() - 1; Index >= 0; --Index)
				{
					UWidget* Painted = Pending[Index];
					UWidget* Parent = PosedParent(Painted);
					if (Pending.Contains(Parent)) continue;
					const FVector2D AParent = Parent ? Starts.FindChecked(Parent).Position : StartRoot.Position;
					const FVector2D BParent = Parent ? Ends.FindChecked(Parent).Position : EndRoot.Position;
					if (const FLayoutBox* A = Starts.Find(Painted)) Ends.Add(Painted, {A->Position - AParent + BParent, A->Size});
					else
					{
						const FLayoutBox B = Ends.FindChecked(Painted);
						Starts.Add(Painted, {B.Position - BParent + AParent, B.Size});
					}
					Pending.RemoveAt(Index);
				}
			}
			return true;
		}

		bool AddLayoutTargetTracks()
		{
			for (const auto& Mapping : Plan.LayoutTargets)
			{
				const FAnimationWidget Painted = Find(Mapping.PresentationWidgetId), To = Find(Mapping.DestinationWidgetId);
				const FLayoutBox A = Starts.FindChecked(Painted.Widget), B = Ends.FindChecked(Painted.Widget);
				if (!AddOffsetTrack(Mapping, Painted, To, A, B) || !AddPropertyTracks(Mapping, Painted, To, A, B)) return false;
			}
			return true;
		}

		bool NativeParentPosition(const FAnimationWidget& Endpoint, const FNativeGeometries& Geometries, FVector2D& Position) const
		{
			const UWidget* Native = Probe.Live(Endpoint.Widget);
			const FGeometry* Geometry = Native ? FNativeLayoutProbe::Geometry(Native->GetParent(), Geometries) : nullptr;
			if (Geometry) Position = LayoutBox(*Geometry).Position;
			return Geometry != nullptr;
		}

		bool AddOffsetTrack(const FUMGPrototypeLayoutTarget& Mapping, const FAnimationWidget& Painted, const FAnimationWidget& To,
			const FLayoutBox& A, const FLayoutBox& B)
		{
			// Shared capture content keeps its own frame, placed when the capture frames are finalized.
			const bool bCaptureContent = Plan.SharedCaptures.ContainsByPredicate([&](const FUMGPrototypeSharedCapture& Item)
				{ return Item.PresentationContentWidgetId == Mapping.PresentationWidgetId; });
			auto* Slot = bCaptureContent ? nullptr : Cast<UCanvasPanelSlot>(Painted.Widget->Slot);
			if (!Slot) return true;
			const FLayoutBox* ParentA = Starts.Find(Slot->Parent);
			const FLayoutBox* ParentB = Ends.Find(Slot->Parent);
			FVector2D ParentPositionA = ParentA ? ParentA->Position : StartRoot.Position;
			FVector2D ParentPositionB = ParentB ? ParentB->Position : EndRoot.Position;
			const FAnimationWidget From = Find(Mapping.NodeId);
			// Synthetic content wrappers retain their native parent basis even without an animation mapping.
			if (From.Widget && To.Widget && Mapping.NodeId != Plan.LayoutSourceWidgetId
				&& ((!ParentA && !NativeParentPosition(From, StartGeometry, ParentPositionA))
					|| (!ParentB && !NativeParentPosition(To, EndGeometry, ParentPositionB))))
				return Fail(TEXT("layout descendant has no arranged native parent geometry"));
			const FVector2D OffsetA = A.Position - ParentPositionA, OffsetB = B.Position - ParentPositionB;
			const FMargin Start(OffsetA.X, OffsetA.Y, A.Size.X, A.Size.Y), End(OffsetB.X, OffsetB.Y, B.Size.X, B.Size.Y);
			Slot->SetAutoSize(false); Slot->SetAnchors(FAnchors(0)); Slot->SetAlignment(FVector2D::ZeroVector); Slot->SetOffsets(Start);
			if (!Mapping.DestinationWidgetId.IsEmpty())
				LayoutSections.Add(Painted.Widget, AddSlotTrack(Animation, Painted.Widget, TEXT("Offsets"), TEXT("LayoutData.Offsets"), Start, End, Keys));
			return true;
		}

		bool AddPropertyTracks(const FUMGPrototypeLayoutTarget& Mapping, const FAnimationWidget& Painted, const FAnimationWidget& To,
			const FLayoutBox& A, const FLayoutBox& B)
		{
			const auto* Change = ChangeOf(Mapping.NodeId);
			if (!Change) return true;
			if (Change->bOpacity && (!Change->bStartPresent || !Change->bEndPresent))
			{
				AddOpacityTrack(Animation, BindWidget(Animation, Painted.Widget), Change->StartOpacity, Change->EndOpacity, Keys);
				return true;
			}
			if (!To.Widget) return true;
			// The socket owns layout and presence; intrinsic properties belong to its single live content.
			FAnimationWidget Owner = Painted;
			if (const auto* Live = Plan.LivePresentations.FindByPredicate([&](const FUMGPrototypeLivePresentation& Item)
				{ return Item.PresentationSocketWidgetId == Mapping.PresentationWidgetId; }))
			{
				Owner = Find(Live->WidgetId);
				if (!Owner.Widget) return Fail(TEXT("live presentation has no unique native content property owner: ") + Live->WidgetId);
			}
			if (Change->bFontSize || Change->bLetterSpacing)
			{
				UTextBlock* SourceText = TextOwner(Owner);
				UTextBlock* TargetText = TextOwner(To);
				if (!SourceText || !TargetText)
					return Fail(FString::Printf(TEXT("measured font layout '%s' -> '%s' has no native glyph pair"), *Mapping.NodeId, *Mapping.DestinationWidgetId));
				if (!Refresh.AddFontTracks(Animation, SourceText, TargetText,
					Change->bFontSize, Change->bLetterSpacing, Keys, Plan.Diagnostic)) return false;
			}
			if (Change->bOpacity)
				AddOpacityTrack(Animation, BindWidget(Animation, Mapping.NodeId == Plan.LayoutSourceWidgetId ? Source.Widget : Owner.Widget),
					Change->StartOpacity, Change->EndOpacity, Keys);
			const auto* Endpoint = Plan.LocalEndpoints.FindByPredicate([&](const FUMGPrototypeLocalEndpoint& Item) { return Item.NodeId == Mapping.NodeId; });
			if (Change->bColor && !Endpoint)
			{
				const FAnimationWidget Visual = ColorOwner(Owner);
				const FString Path = ColorProperty(Visual);
				if (Path.IsEmpty())
					return Fail(FString::Printf(TEXT("measured layout '%s' -> '%s' has no native solid-color owner"), *Mapping.NodeId, *Mapping.DestinationWidgetId));
				AddColorTrack(Animation, BindWidget(Animation, Visual.Widget), ColorPropertyName(Visual.Widget, Path), Path,
					Change->StartColor, Change->EndColor, Keys);
			}
			if (Change->bTextPaintChange && !AddTextPaintTrack(Refresh, Animation, Owner, Change->DestinationWidgetId, Keys, Plan.Diagnostic)) return false;
			if (!FMath::IsNearlyZero(Change->Rotation))
			{
				FWidgetTransform End = Painted.Widget->GetRenderTransform(); End.Angle += Change->Rotation;
				AddTransformTrack(Animation, BindWidget(Animation, Painted.Widget), Painted.Widget->GetRenderTransform(), End, Keys);
			}
			if (Endpoint && !AddLocalEndpointTracks(*Endpoint, *Change, Mapping, Painted, To, A, B)) return false;
			const FAnimationWidget Surface = SurfaceOwner(Owner);
			return !Surface.Widget || Refresh.AddSurfacePaintTrack(Animation, Surface.Widget,
				Change->bSurfacePaintChange ? PaintTargetIndex(Surface.Node->Effect.SurfacePaintTargets, Change->DestinationWidgetId) : INDEX_NONE,
				A.Size, B.Size, Keys, Plan.Diagnostic);
		}

		/** Cross-fades the two native paints of an endpoint that cannot interpolate in place. */
		bool AddLocalEndpointTracks(const FUMGPrototypeLocalEndpoint& Endpoint, const FUMGPrototypeWidgetAnimationTarget& Change,
			const FUMGPrototypeLayoutTarget& Mapping, const FAnimationWidget& Painted, const FAnimationWidget& To,
			const FLayoutBox& A, const FLayoutBox& B)
		{
			const FAnimationWidget Outgoing = Find(Endpoint.SourcePresentationWidgetId), Incoming = Find(Endpoint.TargetPresentationWidgetId);
			if (!Outgoing.Widget || !Incoming.Widget) return Fail(TEXT("local endpoint presentation has no unique native paint pair"));
			if (Endpoint.bFixedPaintSize)
			{
				// Overlay padding follows the common box; each native paint keeps its own logical size and raster UV/offset.
				const FVector2D Pivot = Painted.Widget->GetRenderTransformPivot();
				const FVector2D Delta = B.Size - A.Size;
				const FMargin SourceEnd(Delta.X * Pivot.X, Delta.Y * Pivot.Y, Delta.X * (1 - Pivot.X), Delta.Y * (1 - Pivot.Y));
				const FMargin TargetStart(-SourceEnd.Left, -SourceEnd.Top, -SourceEnd.Right, -SourceEnd.Bottom);
				AddSlotTrack(Animation, Outgoing.Widget, TEXT("Padding"), TEXT("Padding"), FMargin(0), SourceEnd, Keys);
				AddSlotTrack(Animation, Incoming.Widget, TEXT("Padding"), TEXT("Padding"), TargetStart, FMargin(0), Keys);
			}
			AddOpacityTrack(Animation, BindWidget(Animation, Outgoing.Widget), 1, 0, Keys);
			const FGuid IncomingBinding = BindWidget(Animation, Incoming.Widget);
			AddOpacityTrack(Animation, IncomingBinding, 0, 1, Keys);
			AddVisibilityTrack(Animation, IncomingBinding, {{0, ESlateVisibility::Collapsed}, {1, ESlateVisibility::HitTestInvisible}});
			if (!Endpoint.bText) return true;
			UTextBlock* IncomingText = TextOwner(Incoming);
			UTextBlock* StartText = TextOwner(Find(Mapping.NodeId));
			UTextBlock* TargetText = TextOwner(To);
			if (!IncomingText || !StartText || !TargetText) return Fail(TEXT("local text endpoint has no unique native glyph pair"));
			FSlateFontInfo Font = IncomingText->GetFont();
			if (Change.bFontSize) Font.Size = StartText->GetFont().Size;
			if (Change.bLetterSpacing) Font.LetterSpacing = StartText->GetFont().LetterSpacing;
			IncomingText->SetFont(Font);
			return Refresh.AddFontTracks(Animation, IncomingText, TargetText,
					Change.bFontSize, Change.bLetterSpacing, Keys, Plan.Diagnostic)
				&& (!Change.bTextPaintChange || AddTextPaintTrack(Refresh, Animation, Incoming, Change.DestinationWidgetId, Keys, Plan.Diagnostic));
		}

		/** The written value range of a channel, including cubic overshoot. */
		FVector2D Range(double Start, double End) const
		{
			FMovieSceneFloatChannel Channel;
			Keys(Channel, Start, End);
			return WrittenChannelBounds(Channel);
		}

		bool AddSharedCaptureTracks()
		{
			TSet<FName> Rotated;
			for (const auto& Change : Plan.WidgetTargets)
				if (!FMath::IsNearlyZero(Change.Rotation))
					if (const UWidget* Widget = Find(Change.NodeId).Widget) Rotated.Add(Widget->GetFName());
			const auto IsRotated = [&](const UWidget* Widget) { return Rotated.Contains(Widget->GetFName()); };
			for (const auto& Item : Plan.SharedCaptures)
			{
				const auto* Mapping = Plan.LayoutTargets.FindByPredicate([&](const FUMGPrototypeLayoutTarget& Target) { return Target.NodeId == Item.NodeId; });
				const FAnimationWidget Owner = Mapping ? Find(Mapping->PresentationWidgetId) : FAnimationWidget();
				const FAnimationWidget Canonical = Find(Item.CaptureWidgetId), Painted = Find(Item.PresentationCaptureWidgetId);
				const FAnimationWidget Content = Find(Item.PresentationContentWidgetId);
				if (!Owner.Widget || !Cast<URetainerBox>(Canonical.Widget) || !Cast<URetainerBox>(Painted.Widget)
					|| !Content.Widget || !Painted.Node->Effect.SharedCapture.IsSet())
					return Fail(TEXT("shared capture has no unique native owner/content mapping"));
				const FLayoutBox A = Starts.FindChecked(Owner.Widget), B = Ends.FindChecked(Owner.Widget);
				if (A.Size.X <= 0 || A.Size.Y <= 0 || B.Size.X <= 0 || B.Size.Y <= 0)
					return Fail(FString::Printf(TEXT("shared capture owner '%s' -> '%s' has no positive measured native endpoint"),
						*Item.NodeId, *Item.DestinationWidgetId));
				const FVector2D Pivot = Owner.Widget->GetRenderTransformPivot();
				const auto& Data = Painted.Node->Effect.SharedCapture.GetValue();
				FBox2D Bounds(ForceInit);
				if (Data.Source.bClipContent)
				{
					if (Data.Targets.ContainsByPredicate([](const FUMGSharedCaptureEndpoint& Endpoint) { return !Endpoint.bClipContent; }))
						return Fail(TEXT("shared native capture cannot release its source input clip"));
					Bounds += FVector2D::ZeroVector; Bounds += FVector2D(Range(A.Size.X, B.Size.X).Y, Range(A.Size.Y, B.Size.Y).Y);
				}
				else
				{
					const FGeometry* ClipA = FindClipDomain(Probe.Live(Find(Item.NodeId).Widget), StartGeometry, IsRotated);
					const FGeometry* ClipB = FindClipDomain(Probe.Live(Find(Item.DestinationWidgetId).Widget), EndGeometry, IsRotated);
					constexpr const TCHAR* UnstableClip = TEXT("shared native capture has no proven stable source clipping domain");
					if (!ClipA || !ClipB) return Fail(UnstableClip);
					const FLayoutBox DomainA = LayoutBox(*ClipA), DomainB = LayoutBox(*ClipB);
					if (!DomainA.Size.Equals(DomainB.Size) || DomainA.Size.X <= 0 || DomainA.Size.Y <= 0) return Fail(UnstableClip);
					const FVector2D X = Range(A.Position.X - DomainA.Position.X, B.Position.X - DomainB.Position.X);
					const FVector2D Y = Range(A.Position.Y - DomainA.Position.Y, B.Position.Y - DomainB.Position.Y);
					Bounds += FVector2D(-X.Y, -Y.Y); Bounds += FVector2D(DomainA.Size.X - X.X, DomainA.Size.Y - Y.X);
				}
				const FAnimationWidget Shadow = Find(Item.PresentationShadowWidgetId);
				if (Shadow.Node && Shadow.Node->Effect.SharedCapture.IsSet())
				{
					const auto IncludeShadow = [&](const FUMGSharedCaptureEndpoint& Endpoint)
					{
						const FVector2D X = Range((A.Size.X - Endpoint.Size.X) * Pivot.X, (B.Size.X - Endpoint.Size.X) * Pivot.X);
						const FVector2D Y = Range((A.Size.Y - Endpoint.Size.Y) * Pivot.Y, (B.Size.Y - Endpoint.Size.Y) * Pivot.Y);
						IncludeShadowPaint(Bounds, Endpoint, FVector2D(X.X, Y.X), FVector2D(X.Y, Y.Y));
					};
					IncludeShadow(Shadow.Node->Effect.SharedCapture->Source);
					for (const auto& Endpoint : Shadow.Node->Effect.SharedCapture->Targets) IncludeShadow(Endpoint);
				}
				const int32 TargetIndex = Data.Targets.IndexOfByPredicate([&](const FUMGSharedCaptureEndpoint& Endpoint)
					{ return Endpoint.DestinationWidgetId == Item.DestinationWidgetId; });
				if (TargetIndex == INDEX_NONE)
					return Fail(FString::Printf(TEXT("shared capture '%s' presentation '%s' has no native paint target for '%s'"),
						*Item.NodeId, *Item.PresentationCaptureWidgetId, *Item.DestinationWidgetId));
				if (!Refresh.AddSurfacePaintTrack(Animation, Painted.Widget, TargetIndex, A.Size, B.Size, Keys, Plan.Diagnostic)
					|| (Shadow.Widget && !Refresh.AddSurfacePaintTrack(Animation, Shadow.Widget, TargetIndex, A.Size, B.Size, Keys, Plan.Diagnostic)))
					return false;
				const FAnimationWidget CanonicalContent = Find(Item.ContentWidgetId), CanonicalShadow = Find(Item.ShadowWidgetId);
				if (!CanonicalContent.Widget || !Cast<UCanvasPanelSlot>(CanonicalContent.Widget->Slot) || !Cast<UCanvasPanelSlot>(Content.Widget->Slot)
					|| !Cast<UOverlaySlot>(Canonical.Widget->Slot) || !Cast<UOverlaySlot>(Painted.Widget->Slot)
					|| (!Item.ShadowWidgetId.IsEmpty() && (!CanonicalShadow.Widget || !Cast<UOverlaySlot>(CanonicalShadow.Widget->Slot)))
					|| (!Item.PresentationShadowWidgetId.IsEmpty() && (!Shadow.Widget || !Cast<UOverlaySlot>(Shadow.Widget->Slot))))
					return Fail(TEXT("shared capture native role slots are incomplete"));
				const FLayoutBox* ContentA = Starts.Find(Content.Widget);
				const FLayoutBox* ContentB = Ends.Find(Content.Widget);
				if (!ContentA || !ContentB || ContentA->Size.X <= 0 || ContentA->Size.Y <= 0 || ContentB->Size.X <= 0 || ContentB->Size.Y <= 0)
					return Fail(TEXT("shared capture content has no positive measured logical endpoints"));
				RegisterFrame(Canonical, CanonicalShadow, CanonicalContent, Pivot, Bounds);
				FSharedCaptureFrame& Frame = RegisterFrame(Painted, Shadow, Content, Pivot, Bounds);
				if (Shadow.Widget)
				{
					auto* Paints = CastChecked<UPanelWidget>(CastChecked<URetainerBox>(Shadow.Widget)->GetContent())->GetChildAt(0);
					Frame.ContentSections.Add(LayoutSections.FindChecked(Paints));
				}
				const FMargin CaptureStart(0, 0, A.Size.X, A.Size.Y), CaptureEnd(0, 0, B.Size.X, B.Size.Y);
				Frame.CaptureSections.Add(AddSlotTrack(Animation, Painted.Widget, TEXT("Padding"), TEXT("Padding"), CaptureStart, CaptureEnd, Keys));
				if (Shadow.Widget)
					Frame.CaptureSections.Add(AddSlotTrack(Animation, Shadow.Widget, TEXT("Padding"), TEXT("Padding"), CaptureStart, CaptureEnd, Keys));
				// Capture padding spans the owner's outer paint box; its content retains its own layout size.
				Frame.ContentSections.Add(AddSlotTrack(Animation, Content.Widget, TEXT("Offsets"), TEXT("LayoutData.Offsets"),
					FMargin(0, 0, ContentA->Size.X, ContentA->Size.Y), FMargin(0, 0, ContentB->Size.X, ContentB->Size.Y), Keys));
			}
			return true;
		}

		FSharedCaptureFrame& RegisterFrame(const FAnimationWidget& Capture, const FAnimationWidget& Shadow, const FAnimationWidget& Content,
			const FVector2D& Pivot, const FBox2D& Bounds)
		{
			FSharedCaptureFrame& Frame = PendingFrames.FindOrAdd(Capture.Widget);
			Frame.Capture = Capture.Widget; Frame.Shadow = Shadow.Widget; Frame.Content = Content.Widget;
			Frame.SourceSize = Capture.Node->Effect.SharedCapture->Source.Size; Frame.Pivot = Pivot;
			Frame.Bounds += Bounds; Frame.Plans.AddUnique(&Plan);
			return Frame;
		}

		void AddPresentationTracks()
		{
			const bool bLive = !Plan.LivePresentations.IsEmpty();
			const FGuid Binding = BindWidget(Animation, Presentation.Widget);
			AddFloatTrack(Animation, Binding, TEXT("WidthOverride"), TEXT("WidthOverride"), StartRoot.Size.X, EndRoot.Size.X, Keys);
			AddFloatTrack(Animation, Binding, TEXT("HeightOverride"), TEXT("HeightOverride"), StartRoot.Size.Y, EndRoot.Size.Y, Keys);
			const ESlateVisibility Shown = bLive ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible;
			TArray<FVisibilityKey> Visibility = {{0, bLive ? Shown : ESlateVisibility::Collapsed}, {1, Shown}};
			if (!bTemporary) Visibility.Add({Animation->MovieScene->GetPlaybackRange().GetUpperBoundValue() - 1, ESlateVisibility::Collapsed});
			AddVisibilityTrack(Animation, Binding, Visibility);
			// A live presentation remains attached at frame zero until its return callback restores the source paint.
			AddOpacityTrack(Animation, BindWidget(Animation, Original.Widget), Original.Widget->GetRenderOpacity(), 0,
				[bLive](FMovieSceneFloatChannel& Channel, float Start, float End)
				{ Channel.AddConstantKey(0, bLive ? End : Start); Channel.AddConstantKey(1, End); });
			Plan.LayoutOriginalWidgetName = Original.Widget->GetName();
			Plan.LayoutPresentationWidgetName = Presentation.Widget->GetName();
			Plan.LayoutOriginalOpacity = Original.Widget->GetRenderOpacity();
		}

		NativeTextAnimation::FRefreshBuilder& Refresh;
		const FSourceWidgets& Widgets;
		FNativeLayoutProbe& Probe;
		FUMGPrototypeTransitionData& Plan;
		UWidgetAnimation* Animation;
		bool bTemporary;
		FChannelWriter Keys;

		FAnimationWidget Source, Destination, Original, Presentation;
		FNativeGeometries StartGeometry, EndGeometry;
		TMap<UWidget*, FLayoutBox> StartBoxes, EndBoxes;
		TMap<UWidget*, UMovieSceneMarginSection*> LayoutSections;
		FLayoutBox StartRoot, EndRoot;
		// Presentation poses keyed by the painted presentation widget.
		TMap<UWidget*, FLayoutBox> Starts, Ends;
		FSharedCaptureFrames PendingFrames;
	};
	bool AddTextBoxTrack(UWidgetAnimation* Animation, const FAnimationWidget& Source,
		const FAnimationWidget& Destination, FChannelWriter Keys, FString& Error)
	{
		auto* Slot = Cast<UCanvasPanelSlot>(Source.Widget->Slot);
		auto* TargetSlot = Cast<UCanvasPanelSlot>(Destination.Widget->Slot);
		if (!Slot || !TargetSlot)
		{
			Error = TEXT("animated text box requires paired native canvas slots");
			return false;
		}
		if (Slot->GetAutoSize() != TargetSlot->GetAutoSize())
		{
			Error = TEXT("animated text box cannot switch native auto-size mode during playback");
			return false;
		}
		// The natural native size includes the actual compiled font and its resolved tracking.
		FVector2D StartSize(Source.Node->Bounds.W, Source.Node->Bounds.H);
		FVector2D Size(Destination.Node->Bounds.W, Destination.Node->Bounds.H);
		if (Slot->GetAutoSize())
		{
			const TSharedRef<SWidget> StartSlate = Source.Widget->TakeWidget();
			const TSharedRef<SWidget> EndSlate = Destination.Widget->TakeWidget();
			StartSlate->SlatePrepass();
			EndSlate->SlatePrepass();
			const FVector2D StartDesired = StartSlate->GetDesiredSize();
			const FVector2D EndDesired = EndSlate->GetDesiredSize();
			const FAnchors StartAnchors = Slot->GetAnchors();
			const FAnchors EndAnchors = TargetSlot->GetAnchors();
			if (StartAnchors.Minimum.X == StartAnchors.Maximum.X) StartSize.X = StartDesired.X;
			if (StartAnchors.Minimum.Y == StartAnchors.Maximum.Y) StartSize.Y = StartDesired.Y;
			if (EndAnchors.Minimum.X == EndAnchors.Maximum.X) Size.X = EndDesired.X;
			if (EndAnchors.Minimum.Y == EndAnchors.Maximum.Y) Size.Y = EndDesired.Y;
		}
		if (Size.X <= 0 || Size.Y <= 0 || StartSize.X <= 0 || StartSize.Y <= 0)
		{
			Error = TEXT("animated text box has no positive source layout dimensions"); return false;
		}
		const FMargin Start = Slot->GetOffsets();
		FMargin End = Start;
		const FVector2D Delta = FVector2D(Destination.Node->Bounds.X - Source.Node->Bounds.X,
			Destination.Node->Bounds.Y - Source.Node->Bounds.Y);
		const FAnchors Anchors = Slot->GetAnchors();
		const FVector2D Alignment = Slot->GetAlignment();
		if (Anchors.Minimum.X == Anchors.Maximum.X)
		{
			End.Left += Delta.X + (Size.X - StartSize.X) * Alignment.X;
			if (!Slot->GetAutoSize()) End.Right = Size.X;
		}
		else { End.Left += Delta.X; End.Right -= Delta.X + Size.X - StartSize.X; }
		if (Anchors.Minimum.Y == Anchors.Maximum.Y)
		{
			End.Top += Delta.Y + (Size.Y - StartSize.Y) * Alignment.Y;
			if (!Slot->GetAutoSize()) End.Bottom = Size.Y;
		}
		else { End.Top += Delta.Y; End.Bottom -= Delta.Y + Size.Y - StartSize.Y; }
		AddSlotTrack(Animation, Source.Widget, TEXT("Offsets"), TEXT("LayoutData.Offsets"), Start, End, Keys);
		return true;
	}

	bool HasNativeRuntimeClass(const UWidget* Widget)
	{
		UClass* Class = Widget->GetClass();
		if (Class->GetOutermost()->GetName() == TEXT("/Script/UMG")) return true;
		if (!Cast<UWidgetBlueprint>(Class->ClassGeneratedBy)) return false;
		while (Class && !Class->HasAnyClassFlags(CLASS_Native)) Class = Class->GetSuperClass();
		return Class == UUserWidget::StaticClass();
	}

	struct FSpringKey
	{
		FFrameNumber Frame;
		double Value;
		double Velocity;
	};

	bool BuildSpringKeys(const FUMGPrototypeTransitionData& Transition, const UMovieScene* Scene,
		TArray<FSpringKey>& OutKeys, FString& Error)
	{
		const double Frequency = FMath::Sqrt(static_cast<double>(Transition.SpringStiffness) / Transition.SpringMass);
		const double DampingRatio = Transition.SpringDamping
			/ (2 * FMath::Sqrt(static_cast<double>(Transition.SpringMass) * Transition.SpringStiffness));
		const double Decay = DampingRatio < 1 ? DampingRatio * Frequency : Frequency;
		const double Oscillation = DampingRatio < 1 ? Frequency * FMath::Sqrt(1 - DampingRatio * DampingRatio) : 0;
		const double Velocity = Transition.SpringInitialVelocity;
		const double Coefficient = Oscillation > 0 ? (Decay - Velocity) / Oscillation : Frequency - Velocity;
		const auto Evaluate = [&](FFrameNumber Frame)
		{
			const double Time = Scene->GetTickResolution().AsSeconds(Frame);
			const double Envelope = FMath::Exp(-Decay * Time);
			if (Oscillation > 0)
			{
				const double Cosine = FMath::Cos(Oscillation * Time);
				const double Sine = FMath::Sin(Oscillation * Time);
				return FSpringKey{Frame, 1 - Envelope * (Cosine + Coefficient * Sine),
					Envelope * ((Decay - Coefficient * Oscillation) * Cosine
						+ (Oscillation + Decay * Coefficient) * Sine)};
			}
			return FSpringKey{Frame, 1 - Envelope * (1 + Coefficient * Time),
				Envelope * (Frequency * (1 + Coefficient * Time) - Coefficient)};
		};
		// Cubic Hermite interpolation error is bounded by h^4 max|f''''| / 384.
		// The critically damped branch follows the source SpringSolver convention for zeta >= 1.
		constexpr double ProgressTolerance = 0.0001;
		constexpr int32 MaximumKeys = 4096;
		const auto Bound = [&](FFrameNumber First, FFrameNumber Last)
		{
			const double Start = Scene->GetTickResolution().AsSeconds(First);
			const double End = Scene->GetTickResolution().AsSeconds(Last);
			const double Derivative = Oscillation > 0
				? FMath::Pow(Frequency, 4) * FMath::Sqrt(1 + Coefficient * Coefficient)
				: FMath::Pow(Frequency, 4) * (1 + FMath::Abs(Coefficient) * End)
					+ 4 * FMath::Pow(Frequency, 3) * FMath::Abs(Coefficient);
			return FMath::Pow(End - Start, 4) * FMath::Exp(-Decay * Start) * Derivative / 384;
		};
		const FFrameNumber End(FMath::Max(1, PrototypeSecondsToFrame(Scene, Transition.DurationSeconds).Value));
		if (!FMath::IsFinite(Frequency) || !FMath::IsFinite(Coefficient) || End.Value < 1)
		{
			Error = TEXT("spring parameters cannot be represented by the native animation timeline");
			return false;
		}
		OutKeys.Add(Evaluate(0));
		double LargestBound = 0;
		TFunction<bool(FFrameNumber, FFrameNumber)> Subdivide = [&](FFrameNumber First, FFrameNumber Last)
		{
			const double ErrorBound = Bound(First, Last);
			if (!FMath::IsFinite(ErrorBound) || OutKeys.Num() >= MaximumKeys) return false;
			if (ErrorBound <= ProgressTolerance)
			{
				LargestBound = FMath::Max(LargestBound, ErrorBound);
				OutKeys.Add(Evaluate(Last));
				return true;
			}
			if (Last.Value - First.Value <= 1) return false;
			const FFrameNumber Middle(First.Value + (Last.Value - First.Value) / 2);
			return Subdivide(First, Middle) && Subdivide(Middle, Last);
		};
		if (!Subdivide(0, End))
		{
			Error = TEXT("spring exceeds native keyframe precision or key budget; action not generated");
			return false;
		}
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Native spring: %d keys, progress error bound %.8g, terminal offset %.8g"),
			OutKeys.Num(), LargestBound, OutKeys.Last().Value - 1);
		return true;
	}

	void WriteAnimationChannel(const FUMGPrototypeTransitionData& Transition, const UMovieScene* Scene,
		const TArray<FSpringKey>& SpringKeys, FMovieSceneFloatChannel& Channel, float Start, float Finish)
	{
		const FFrameNumber End(FMath::Max(1, PrototypeSecondsToFrame(Scene, Transition.DurationSeconds).Value));
		Channel.SetTickResolution(Scene->GetTickResolution());
		if (Transition.bSpring)
		{
			for (const FSpringKey& Key : SpringKeys)
			{
				FMovieSceneTangentData Tangent;
				Tangent.ArriveTangent = Tangent.LeaveTangent = (Finish - Start) * Key.Velocity
					/ Scene->GetTickResolution().AsDecimal();
				Channel.AddCubicKey(Key.Frame, FMath::Lerp(Start, Finish, Key.Value), RCTM_User, Tangent);
			}
			return;
		}
		if (!Transition.bCubicBezier || FMath::IsNearlyEqual(Start, Finish))
		{
			Channel.AddLinearKey(0, Start);
			Channel.AddLinearKey(End, Finish);
			return;
		}
		const auto& Curve = Transition.CubicBezier;
		const double Duration = Scene->GetTickResolution().AsSeconds(End);
		const double Difference = Finish - Start;
		FMovieSceneTangentData Leave;
		Leave.TangentWeightMode = RCTWM_WeightedLeave;
		Leave.LeaveTangent = Curve.X == 0 ? 0 : Difference * Curve.Y / (End.Value * Curve.X);
		Leave.LeaveTangentWeight = FMath::Sqrt(FMath::Square(Duration * Curve.X) + FMath::Square(Difference * Curve.Y));
		FMovieSceneTangentData Arrive;
		Arrive.TangentWeightMode = RCTWM_WeightedArrive;
		Arrive.ArriveTangent = Curve.Z == 1 ? 0 : Difference * (1 - Curve.W) / (End.Value * (1 - Curve.Z));
		Arrive.ArriveTangentWeight = FMath::Sqrt(FMath::Square(Duration * (1 - Curve.Z)) + FMath::Square(Difference * (1 - Curve.W)));
		Channel.AddCubicKey(0, Start, RCTM_User, Leave);
		Channel.AddCubicKey(End, Finish, RCTM_User, Arrive);
	}

	void GenerateWidgetAnimation(UWidgetBlueprint* Blueprint, const FSourceWidgets& SourceWidgets, FNativeLayoutProbe& Probe,
		NativeTextAnimation::FRefreshBuilder& Refresh, FUMGPrototypeTransitionData& Transition, int32 BindingIndex, bool bTemporary, FSharedCaptureFrames& SharedFrames)
	{
		if (!Transition.Diagnostic.IsEmpty()) return;
		for (auto& Order : Transition.LayerOrders)
		{
			Order.WidgetName.Reset();
			Order.ParentWidgetName.Reset();
			const auto* ChildLayout = Transition.bFixedViewportLayout ? Transition.LayoutTargets.FindByPredicate(
				[&](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == Order.NodeId; }) : nullptr;
			const auto* ParentLayout = Transition.bFixedViewportLayout ? Transition.LayoutTargets.FindByPredicate(
				[&](const FUMGPrototypeLayoutTarget& Item) { return Item.NodeId == Order.ParentNodeId; }) : nullptr;
			UWidget* Child = SourceWidgets.Find(ChildLayout ? ChildLayout->PresentationWidgetId : Order.NodeId).Widget;
			UWidget* Scope = SourceWidgets.Find(ParentLayout ? ParentLayout->PresentationWidgetId : Order.ParentNodeId).Widget;
			if (!Child || !Scope)
			{
				Transition.Diagnostic = TEXT("source draw order requires unique native child and parent identities");
				return;
			}
			UWidget* Parent = Child->GetParent();
			UWidget* Ancestor = Parent;
			while (Ancestor && Ancestor != Scope) Ancestor = Ancestor->GetParent();
			if (!Parent || !Ancestor)
			{
				Transition.Diagnostic = TEXT("source draw-order child is outside its generated parent subtree");
				return;
			}
			Order.WidgetName = Child->GetName();
			Order.ParentWidgetName = Parent->GetName();
		}
		TArray<FAnimationWidget> Widgets;
		TArray<FAnimationWidget> ColorWidgets;
		for (const auto& Target : Transition.WidgetTargets)
		{
			if (Transition.bFixedViewportLayout) break;
			const FAnimationWidget Match = SourceWidgets.Find(Target.NodeId);
			if (!Match.Widget || !HasNativeRuntimeClass(Match.Widget))
			{
				Transition.Diagnostic = FString::Printf(TEXT("source node '%s' has no unique native widget/property target"), *Target.NodeId);
				return;
			}
			Widgets.Add(Match);
			const FAnimationWidget Visual = ColorOwner(Match);
			if (Target.bColor && (!HasNativeRuntimeClass(Visual.Widget) || ColorProperty(Visual).IsEmpty()))
			{
				Transition.Diagnostic = FString::Printf(TEXT("source node '%s' has no supported native solid-color owner"), *Target.NodeId);
				return;
			}
			ColorWidgets.Add(Visual);
		}
		if (Widgets.IsEmpty() && Transition.LayerOrders.IsEmpty() && !Transition.bFixedViewportLayout) return;
		const FName Name = MakePrototypeAnimationName(BindingIndex, TEXT("properties"));
		auto* Animation = CreateAnimation(Blueprint, Name, Transition.DurationSeconds);
		if (!Animation) { Transition.Diagnostic = TEXT("animation duration is not a finite non-negative value"); return; }
		UMovieScene* Scene = Animation->MovieScene;
		TArray<FSpringKey> SpringKeys;
		if (Transition.bSpring && (!Widgets.IsEmpty() || Transition.bFixedViewportLayout) && !BuildSpringKeys(Transition, Scene, SpringKeys, Transition.Diagnostic))
		{
			RemoveAnimation(Blueprint, Animation);
			return;
		}
		const auto Keys = [&](FMovieSceneFloatChannel& Channel, float Start, float Finish)
		{
			WriteAnimationChannel(Transition, Scene, SpringKeys, Channel, Start, Finish);
		};
		if (Transition.bFixedViewportLayout)
		{
			if (!FFixedLayoutAnimation(Refresh, SourceWidgets, Probe, Transition, Animation, bTemporary, Keys).Build(SharedFrames))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Native fixed layout animation '%s' was not generated: %s"), *Name.ToString(), *Transition.Diagnostic);
				RemoveAnimation(Blueprint, Animation); return;
			}
			Transition.WidgetAnimationName = Name.ToString();
			return;
		}
		const auto AddTargetTracks = [&](const FAnimationWidget& Source, const FAnimationWidget& Visual, const FUMGPrototypeWidgetAnimationTarget& Target)
		{
			const FAnimationWidget Destination = SourceWidgets.Find(Target.DestinationWidgetId);
			if (Target.bSurfacePaintChange)
			{
				const FAnimationWidget Surface = SurfaceOwner(Source);
				const int32 PaintIndex = Surface.Node ? PaintTargetIndex(Surface.Node->Effect.SurfacePaintTargets, Target.DestinationWidgetId) : INDEX_NONE;
				if (!Surface.Widget || PaintIndex == INDEX_NONE || !Destination.Widget)
				{
					Transition.Diagnostic = TEXT("animated surface has no unique native producer and destination paints");
					return false;
				}
				if (!Refresh.AddSurfacePaintTrack(Animation, Surface.Widget, PaintIndex,
					FVector2D(Source.Node->Bounds.W, Source.Node->Bounds.H), FVector2D(Destination.Node->Bounds.W, Destination.Node->Bounds.H),
					Keys, Transition.Diagnostic)) return false;
			}
			if (Target.bTextPaintChange)
			{
				if (!TextOwner(Source)) { Transition.Diagnostic = TEXT("animated text paint has no native text owner"); return false; }
				if (!AddTextPaintTrack(Refresh, Animation, Source, Target.DestinationWidgetId, Keys, Transition.Diagnostic)) return false;
			}
			if (Target.bFontSize || Target.bLetterSpacing || Target.bTextBoxChange)
			{
				UTextBlock* SourceText = TextOwner(Source);
				UTextBlock* DestinationText = Destination.Widget ? TextOwner(Destination) : nullptr;
				if (!SourceText || !DestinationText)
				{
					Transition.Diagnostic = TEXT("animated text has no unique source and destination native TextBlock");
					return false;
				}
				if (!Refresh.AddFontTracks(Animation, SourceText, DestinationText,
						Target.bFontSize, Target.bLetterSpacing, Keys, Transition.Diagnostic)
					|| (Target.bTextBoxChange && !AddTextBoxTrack(Animation, Source, Destination, Keys, Transition.Diagnostic))) return false;
			}
			const FWidgetTransform Start = Source.Widget->GetRenderTransform();
			FWidgetTransform Finish = Start;
			if (!Target.bTextBoxChange)
			{
				Finish.Translation += Target.Translation;
				Finish.Scale *= Target.Scale;
			}
			Finish.Angle += Target.Rotation;
			const FGuid Guid = Start != Finish || Target.bOpacity ? BindWidget(Animation, Source.Widget) : FGuid();
			if (Start != Finish) AddTransformTrack(Animation, Guid, Start, Finish, Keys);
			if (Target.bOpacity) AddOpacityTrack(Animation, Guid, Target.StartOpacity, Target.EndOpacity, Keys);
			if (Target.bColor)
			{
				const FString Path = ColorProperty(Visual);
				const FGuid ColorGuid = Visual.Widget == Source.Widget && Guid.IsValid() ? Guid : BindWidget(Animation, Visual.Widget);
				AddColorTrack(Animation, ColorGuid, ColorPropertyName(Visual.Widget, Path), Path, Target.StartColor, Target.EndColor, Keys);
			}
			return true;
		};
		for (int32 TargetIndex = 0; TargetIndex < Widgets.Num(); ++TargetIndex)
			if (!AddTargetTracks(Widgets[TargetIndex], ColorWidgets[TargetIndex], Transition.WidgetTargets[TargetIndex]))
			{
				RemoveAnimation(Blueprint, Animation);
				return;
			}
		Transition.WidgetAnimationName = Name.ToString();
	}
	void GeneratePageAnimation(UWidgetBlueprint* Blueprint, const FUMGWidgetNode& Root, const FSourceWidgets& SourceWidgets,
		const FUMGPrototypeNavigationData& Navigation, FUMGPrototypeNavigationBinding& Binding, int32 Index)
	{
		auto& Transition = Binding.PrimaryStep().Transition;
		if (Binding.Steps.Num() != 1 || !Transition.IsAnimated() || !Transition.Diagnostic.IsEmpty()) return;
		const bool bOverlay = Binding.Steps[0].Kind == EUMGPrototypeNavigationStepKind::Overlay;
		if (!bOverlay && Binding.Steps[0].Kind != EUMGPrototypeNavigationStepKind::Navigate) return;
		const FString& Type = Transition.Type;
		if (Type != TEXT("DISSOLVE") && Type != TEXT("PUSH") && Type != TEXT("MOVE_IN")
			&& Type != TEXT("MOVE_OUT") && Type != TEXT("SLIDE_IN") && Type != TEXT("SLIDE_OUT")) return;
		UWidget* Outgoing = SourceWidgets.Find(Binding.SourcePageId).Widget;
		UWidget* Incoming = SourceWidgets.Find(Binding.Steps[0].TargetPageId).Widget;
		if ((!bOverlay && !Outgoing) || !Incoming)
		{
			Transition.Diagnostic = TEXT("page transition requires unique native source and target pages");
			return;
		}
		const FUMGWidgetNode* Host = FindWidgetNodeById(Root, Navigation.PageHostWidgetId);
		const FVector2D Offset = Host ? ResolvePrototypeAnimationOffset(Host->Bounds, Transition.Direction) : FVector2D::ZeroVector;
		if (Type != TEXT("DISSOLVE") && (Offset.IsNearlyZero() || !FMath::IsFinite(Offset.X) || !FMath::IsFinite(Offset.Y)))
		{
			Transition.Diagnostic = TEXT("moving page transition requires real page-host dimensions and source direction");
			return;
		}
		auto* Animation = CreateAnimation(Blueprint, MakePrototypeAnimationName(Index, TEXT("pages")), Transition.DurationSeconds);
		if (!Animation) { Transition.Diagnostic = TEXT("animation duration is not a finite non-negative value"); return; }
		TArray<FSpringKey> SpringKeys;
		if (Transition.bSpring && !BuildSpringKeys(Transition, Animation->MovieScene, SpringKeys, Transition.Diagnostic))
		{
			RemoveAnimation(Blueprint, Animation);
			return;
		}
		const auto Keys = [&](FMovieSceneFloatChannel& Channel, float From, float To)
			{ WriteAnimationChannel(Transition, Animation->MovieScene, SpringKeys, Channel, From, To); };
		const FWidgetTransform InEnd = Incoming->GetRenderTransform();
		FWidgetTransform InStart = InEnd;
		float InOpacity = Incoming->GetRenderOpacity();
		if (Type == TEXT("PUSH") || Type == TEXT("MOVE_IN") || Type == TEXT("SLIDE_IN")) InStart.Translation -= Offset;
		if (Type == TEXT("DISSOLVE") || Type == TEXT("SLIDE_IN")) InOpacity = 0;
		const FGuid IncomingBinding = BindWidget(Animation, Incoming);
		if (InStart != InEnd) AddTransformTrack(Animation, IncomingBinding, InStart, InEnd, Keys);
		if (!FMath::IsNearlyEqual(InOpacity, Incoming->GetRenderOpacity()))
			AddOpacityTrack(Animation, IncomingBinding, InOpacity, Incoming->GetRenderOpacity(), Keys);
		if (!bOverlay)
		{
			const FWidgetTransform OutStart = Outgoing->GetRenderTransform();
			FWidgetTransform OutEnd = OutStart;
			float OutOpacity = Outgoing->GetRenderOpacity();
			if (Type == TEXT("PUSH") || Type == TEXT("MOVE_OUT") || Type == TEXT("SLIDE_OUT")) OutEnd.Translation += Offset;
			if (Type == TEXT("DISSOLVE") || Type == TEXT("SLIDE_OUT")) OutOpacity = 0;
			const FGuid OutgoingBinding = BindWidget(Animation, Outgoing);
			if (OutStart != OutEnd) AddTransformTrack(Animation, OutgoingBinding, OutStart, OutEnd, Keys);
			if (!FMath::IsNearlyEqual(OutOpacity, Outgoing->GetRenderOpacity()))
				AddOpacityTrack(Animation, OutgoingBinding, Outgoing->GetRenderOpacity(), OutOpacity, Keys);
		}
		Transition.WidgetAnimationName = Animation->GetName();
	}

}

UWidgetAnimation* CreateAnimation(UWidgetBlueprint* Blueprint, FName Name, float DurationSeconds)
{
	if (!Blueprint || !FMath::IsFinite(DurationSeconds) || DurationSeconds < 0) return nullptr;
	Blueprint->Modify();
	auto* Animation = NewObject<UWidgetAnimation>(Blueprint, Name, RF_Transactional);
	Animation->SetDisplayLabel(Name.ToString());
	Animation->MovieScene = NewObject<UMovieScene>(Animation, Name, RF_Transactional);
	UMovieScene* Scene = Animation->MovieScene;
	Scene->SetDisplayRate(FFrameRate(60, 1));
	const FFrameNumber End(FMath::Max(1, PrototypeSecondsToFrame(Scene, DurationSeconds).Value));
	const double NativeDuration = Scene->GetTickResolution().AsSeconds(End);
	Scene->SetPlaybackRange(TRange<FFrameNumber>(0, End.Value + 1));
	Scene->GetEditorData().WorkStart = 0;
	Scene->GetEditorData().WorkEnd = NativeDuration;
	Scene->SetViewRange(0, NativeDuration);
	Scene->SetWorkingRange(0, NativeDuration);
	Blueprint->Animations.Add(Animation);
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
	DesignWidgetBlueprintUtils::EnsureWidgetVariableGuids(Blueprint);
	Blueprint->OnVariableAdded(Name);
#endif
	return Animation;
}

FGuid BindWidget(UWidgetAnimation* Animation, UWidget* Widget)
{
	const FGuid Guid = Animation->MovieScene->AddPossessable(Widget->GetName(), Widget->GetClass());
	FWidgetAnimationBinding Binding;
	Binding.AnimationGuid = Guid;
	Binding.WidgetName = Widget->GetFName();
	Animation->AnimationBindings.Add(Binding);
	return Guid;
}

void AddTransformTrack(UWidgetAnimation* Animation, const FGuid& Binding,
	const FWidgetTransform& Start, const FWidgetTransform& End, FChannelWriter WriteChannel)
{
	auto* Track = Animation->MovieScene->AddTrack<UMovieScene2DTransformTrack>(Binding);
	Track->SetPropertyNameAndPath(TEXT("RenderTransform"), TEXT("RenderTransform"));
	auto* Section = CastChecked<UMovieScene2DTransformSection>(Track->CreateNewSection());
	Section->SetRange(Animation->MovieScene->GetPlaybackRange());
	Section->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
	Section->SetMask(FMovieScene2DTransformMask(EMovieScene2DTransformChannel::Translation
		| EMovieScene2DTransformChannel::Scale | EMovieScene2DTransformChannel::Shear | EMovieScene2DTransformChannel::Rotation));
	WriteChannel(Section->Translation[0], Start.Translation.X, End.Translation.X);
	WriteChannel(Section->Translation[1], Start.Translation.Y, End.Translation.Y);
	WriteChannel(Section->Scale[0], Start.Scale.X, End.Scale.X);
	WriteChannel(Section->Scale[1], Start.Scale.Y, End.Scale.Y);
	WriteChannel(Section->Shear[0], Start.Shear.X, End.Shear.X);
	WriteChannel(Section->Shear[1], Start.Shear.Y, End.Shear.Y);
	WriteChannel(Section->Rotation, Start.Angle, End.Angle);
	Track->AddSection(*Section);
}

void AddFloatTrack(UWidgetAnimation* Animation, const FGuid& Binding, FName PropertyName,
	const FString& PropertyPath, float Start, float End, FChannelWriter WriteChannel)
{
	auto* Track = Animation->MovieScene->AddTrack<UMovieSceneFloatTrack>(Binding);
	Track->SetPropertyNameAndPath(PropertyName, PropertyPath);
	auto* Section = CastChecked<UMovieSceneFloatSection>(Track->CreateNewSection());
	Section->SetRange(Animation->MovieScene->GetPlaybackRange());
	Section->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
	WriteChannel(*Section->GetChannelProxy().GetChannel<FMovieSceneFloatChannel>(0), Start, End);
	Track->AddSection(*Section);
}

void AddOpacityTrack(UWidgetAnimation* Animation, const FGuid& Binding,
	float Start, float End, FChannelWriter WriteChannel)
{
	AddFloatTrack(Animation, Binding, TEXT("RenderOpacity"), TEXT("RenderOpacity"), Start, End, WriteChannel);
}

void AddColorTrack(UWidgetAnimation* Animation, const FGuid& Binding, FName PropertyName,
	const FString& PropertyPath, const FLinearColor& Start, const FLinearColor& End, FChannelWriter WriteChannel)
{
	auto* Track = Animation->MovieScene->AddTrack<UMovieSceneColorTrack>(Binding);
	Track->SetPropertyNameAndPath(PropertyName, PropertyPath);
	auto* Section = CastChecked<UMovieSceneColorSection>(Track->CreateNewSection());
	Section->SetRange(Animation->MovieScene->GetPlaybackRange());
	Section->SetCompletionMode(EMovieSceneCompletionMode::KeepState);
	WriteChannel(Section->GetRedChannel(), Start.R, End.R);
	WriteChannel(Section->GetGreenChannel(), Start.G, End.G);
	WriteChannel(Section->GetBlueChannel(), Start.B, End.B);
	WriteChannel(Section->GetAlphaChannel(), Start.A, End.A);
	Track->AddSection(*Section);
}

void RemoveAnimation(UWidgetBlueprint* Blueprint, UWidgetAnimation* Animation)
{
	NativeTextAnimation::RemoveGeneratedEndpoints(Blueprint, Animation);
	Blueprint->Modify();
	Animation->Modify();
	Blueprint->Animations.Remove(Animation);
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
	Blueprint->WidgetVariableNameToGuidMap.Remove(Animation->GetFName());
#endif
	Animation->Rename(nullptr, GetTransientPackage());
}

bool IsGeneratedNavigationAnimation(const UWidgetAnimation* Animation)
{
	return Animation && Animation->GetName().StartsWith(PrototypeAnimationPrefix);
}

void ResolvePreparedPlacements(UWidgetBlueprint* Blueprint, const TMap<UWidget*, UWidget*>& Placements,
	FUMGPrototypeNavigationData* NavigationData)
{
	if (Placements.IsEmpty()) return;
	TMap<FName, UWidget*> PlacementNames;
	for (const auto& Placement : Placements) PlacementNames.Add(Placement.Key->GetFName(), Placement.Value);
	if (NavigationData)
		for (auto& Binding : NavigationData->ClickBindings)
		{
			if (UWidget* const* Placement = PlacementNames.Find(FName(*Binding.PrimaryStep().Transition.LayoutOriginalWidgetName)))
				Binding.PrimaryStep().Transition.LayoutOriginalWidgetName = (*Placement)->GetName();
			for (auto& Live : Binding.PrimaryStep().Transition.LivePresentations)
				if (UWidget* const* Placement = PlacementNames.Find(FName(*Live.WidgetName)))
					Live.WidgetName = (*Placement)->GetName();
		}
	for (UWidgetAnimation* Animation : Blueprint->Animations)
	{
		if (!IsGeneratedNavigationAnimation(Animation) || !Animation->MovieScene) continue;
		UMovieScene* Scene = Animation->MovieScene;
		const TArray<FWidgetAnimationBinding> OriginalBindings = Animation->AnimationBindings;
		for (const auto& Binding : OriginalBindings)
		{
			UWidget* const* Placement = PlacementNames.Find(Binding.WidgetName);
			const FMovieSceneBinding* Source = Scene->FindBinding(Binding.AnimationGuid);
			if (!Placement || !Source) continue;
			if (!Binding.SlotWidgetName.IsNone())
			{
				for (auto& NativeBinding : Animation->AnimationBindings)
					if (NativeBinding.AnimationGuid == Binding.AnimationGuid)
					{
						NativeBinding.WidgetName = (*Placement)->GetFName();
						NativeBinding.SlotWidgetName = (*Placement)->GetParent()->GetFName();
					}
				continue;
			}
			TArray<UMovieScenePropertyTrack*> Tracks;
			for (UMovieSceneTrack* Track : Source->GetTracks())
				if (auto* Property = Cast<UMovieScenePropertyTrack>(Track))
					if (Property->GetPropertyPath() == TEXT("RenderTransform") || Property->GetPropertyPath() == TEXT("RenderOpacity"))
						Tracks.Add(Property);
			if (Tracks.IsEmpty()) continue;
			const FGuid PlacementBinding = BindWidget(Animation, *Placement);
			for (auto* Track : Tracks)
			{
				Scene->RemoveTrack(*Track);
				if (Track->GetPropertyPath() == TEXT("RenderOpacity"))
					Track->SetPropertyNameAndPath(TEXT("SourceOpacity"), TEXT("SourceOpacity"));
				Scene->AddGivenTrack(Track, PlacementBinding);
			}
			if (Scene->FindBinding(Binding.AnimationGuid)->GetTracks().IsEmpty())
			{
				Scene->RemovePossessable(Binding.AnimationGuid);
				Animation->AnimationBindings.RemoveAll([&](const FWidgetAnimationBinding& Candidate)
				{
					return Candidate.AnimationGuid == Binding.AnimationGuid;
				});
			}
		}
	}
	if (!NavigationData) return;
	for (auto& Binding : NavigationData->ClickBindings)
		for (auto& Order : Binding.PrimaryStep().Transition.LayerOrders)
			if (UWidget* const* Placement = PlacementNames.Find(*Order.WidgetName))
			{
				Order.WidgetName = (*Placement)->GetName();
				Order.ParentWidgetName = (*Placement)->GetParent() ? (*Placement)->GetParent()->GetName() : FString();
			}
}

void RemoveGeneratedAnimations(UWidgetBlueprint* WidgetBlueprint)
{
	if (!WidgetBlueprint)
	{
		return;
	}

	WidgetBlueprint->Modify();
	TArray<UWidgetAnimation*> StaleAnimations;
	WidgetBlueprint->Animations.RemoveAll([&StaleAnimations](UWidgetAnimation* Animation)
	{
		if (IsGeneratedNavigationAnimation(Animation))
		{
			StaleAnimations.Add(Animation);
			return true;
		}
		return false;
	});
	for (UWidgetAnimation* Animation : StaleAnimations)
	{
		RemoveAnimation(WidgetBlueprint, Animation);
	}
	if (StaleAnimations.Num() > 0)
	{
		UE_LOG(LogDesign2UMG, Verbose,
			TEXT("PrototypeNavigation animations: removed %d stale generated animation(s) before regenerating"),
			StaleAnimations.Num());
	}
}

void GenerateNavigationAnimations(
	UWidgetBlueprint* WidgetBlueprint,
	const FUMGWidgetNode& RootNode,
	FUMGPrototypeNavigationData& NavigationData)
{
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return;
	}
	TMap<FName, FString> TargetIds;
	VisitSourceWidgets(RootNode, WidgetBlueprint->WidgetTree->RootWidget, [&](const FUMGWidgetNode& Node, UWidget* Widget)
	{
		if (!Node.Id.IsEmpty()) TargetIds.Add(Widget->GetFName(), Node.Id);
	});
	Design2UMGAnimationPresets::RecordSourceTargets(WidgetBlueprint, TargetIds);
	RemoveGeneratedAnimations(WidgetBlueprint);

	if (!NavigationData.IsValid())
	{
		return;
	}
	const FSourceWidgets SourceWidgets(RootNode, WidgetBlueprint->WidgetTree->RootWidget);
	const auto ResolveName = [&](const FString& Id, FString& Name)
	{
		const UWidget* Widget = SourceWidgets.Find(Id).Widget;
		Name = Widget ? Widget->GetName() : FString();
	};
	for (auto& Host : NavigationData.StateHosts)
		for (auto& State : Host.States) ResolveName(State.WidgetId, State.WidgetName);
	for (auto& Binding : NavigationData.ClickBindings)
		for (auto& Live : Binding.PrimaryStep().Transition.LivePresentations)
		{
			ResolveName(Live.WidgetId, Live.WidgetName);
			ResolveName(Live.OriginalSocketWidgetId, Live.OriginalSocketWidgetName);
			ResolveName(Live.PresentationSocketWidgetId, Live.PresentationSocketWidgetName);
		}

	// Regeneration replaces generated animations and recomputes their names.
	for (FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
	{
		Binding.PrimaryStep().Transition.WidgetAnimationName.Reset();
	}
	const auto Drop = [&](FUMGPrototypeTransitionData& Plan, const FString& Error)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Removing native animation '%s' for source '%s', destination '%s': %s"),
			*Plan.WidgetAnimationName, *Plan.LayoutSourceWidgetId, *Plan.LayoutDestinationWidgetId, *Error);
		Plan.Diagnostic = Error;
		if (!Plan.WidgetAnimationName.IsEmpty())
			if (auto* Animation = FindObject<UWidgetAnimation>(WidgetBlueprint, *Plan.WidgetAnimationName)) RemoveAnimation(WidgetBlueprint, Animation);
		Plan.WidgetAnimationName.Reset();
	};
	NativeTextAnimation::FRefreshBuilder Refresh(WidgetBlueprint);
	FSharedCaptureFrames SharedFrames;
	{
		// Transitions measure source layout, which their generated tracks never change; one compile serves them all.
		FNativeLayoutProbe Probe(WidgetBlueprint, RootNode);
		for (int32 Index = 0; Index < NavigationData.ClickBindings.Num(); ++Index)
		{
			auto& Binding = NavigationData.ClickBindings[Index];
			if ((Design2UMGPrototypeGraphWiring::IsSimpleMouseActionBinding(Binding) || Binding.UsesPageOpenTrigger())
				&& Binding.Steps.Num() == 1 && (!Binding.PrimaryStep().Transition.WidgetTargets.IsEmpty() || !Binding.PrimaryStep().Transition.LayerOrders.IsEmpty() || Binding.PrimaryStep().Transition.bFixedViewportLayout))
			{
				GenerateWidgetAnimation(WidgetBlueprint, SourceWidgets, Probe, Refresh, Binding.PrimaryStep().Transition, Index,
					Binding.UsesHoverTrigger() || Binding.UsesPressTrigger(), SharedFrames);
			}
		}
	}
	// Static captures may sit in presentation layers the transitions just placed, so they measure a fresh compile.
	FNativeLayoutProbe StaticProbe(WidgetBlueprint, RootNode);
	FString CaptureError;
	if (!RegisterStaticSharedCaptures(RootNode, WidgetBlueprint->WidgetTree->RootWidget, StaticProbe, SharedFrames, CaptureError)
		|| !FinalizeSharedCaptureFrames(SharedFrames, CaptureError))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Native shared capture finalization failed for '%s': %s"),
			*WidgetBlueprint->GetPathName(), *CaptureError);
		for (auto& Pair : SharedFrames) for (auto* Plan : Pair.Value.Plans) Drop(*Plan, CaptureError);
	}
	Refresh.Build([&](UWidgetAnimation* Animation, const FString& Error)
	{
		const FString Name = Animation->GetName();
		for (auto& Binding : NavigationData.ClickBindings)
			if (Binding.PrimaryStep().Transition.WidgetAnimationName == Name) Drop(Binding.PrimaryStep().Transition, Error);
	});
	for (int32 Index = 0; Index < NavigationData.ClickBindings.Num(); ++Index)
		GeneratePageAnimation(WidgetBlueprint, RootNode, SourceWidgets, NavigationData, NavigationData.ClickBindings[Index], Index);
}

} // namespace UMGPrototypeAnimationBuilder
