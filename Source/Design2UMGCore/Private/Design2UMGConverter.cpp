// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "Design2UMGGridLayout.h"
#include "Design2UMGNodeWidgetFactory.h"
#include "Design2UMGPrototypeNavigation.h"
#include "Design2UMGSourceMetadata.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "ImageFillUtils.h"
#include "LayoutMapper.h"
#include "SizeEstimator.h"
#include "SharedCoreSemantics.h"
#include "TextMeasurement.h"
#include "VisualMapper.h"
#include "RefExpander.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "Misc/Crc.h"

namespace
{
	constexpr int32 MaxTreeRecursionDepth = 512;

	struct FConversionVisualSettingsPayload
	{
		UDesign2UMGSettings::FResolvedPlaceholderHatchStyle PlaceholderHatchStyle;
		FVisualMapperOptions VisualMapperOptions;
	};

	struct FConversionSettingsSnapshot
	{
		FConversionVisualSettingsPayload Visual;
		Design2UMGTextWidgetBuilder::FIconFontSettingsSnapshot IconFont;
	};

	FConversionSettingsSnapshot BuildConversionSettingsSnapshot()
	{
		FConversionSettingsSnapshot Snapshot;
		if (const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get())
		{
			Snapshot.Visual.PlaceholderHatchStyle = Settings->ResolvePlaceholderHatchStyle();
			Snapshot.Visual.VisualMapperOptions.LuminosityFitOpacityCap = Settings->ResolveLuminosityFitOpacityCap();
			Snapshot.IconFont = Design2UMGTextWidgetBuilder::BuildIconFontSettingsSnapshot(Settings);
		}
		else
		{
			Snapshot.Visual.PlaceholderHatchStyle = UDesign2UMGSettings::FResolvedPlaceholderHatchStyle();
			Snapshot.Visual.VisualMapperOptions.LuminosityFitOpacityCap = UDesign2UMGSettings::CanonicalLuminosityFitOpacityCap;
		}
		return Snapshot;
	}

	struct FConversionState
	{
		explicit FConversionState(
			const FDesignDocument& InDocument,
			TArray<FString>& InWarnings,
			const FConversionSettingsSnapshot& InSettingsSnapshot)
			: Document(InDocument)
			, Warnings(InWarnings)
			, SettingsSnapshot(InSettingsSnapshot)
		{
		}

		const FDesignDocument& Document;
		TArray<FString>& Warnings;
		const FConversionSettingsSnapshot& SettingsSnapshot;
		TSet<FString> WarnedFamilies;
		TSet<FString> PrunedInvalidMappings;
		TSet<FString> PrototypeTriggerNodeIds;
		TMap<FString, FString> PendingIconFontMappings;
		FUMGPrototypeNavigationData PrototypeNavigation;
		int32 RecursionDepth = 0;
	};

	struct FResolvedPrototypeAction
	{
		EUMGPrototypeNavigationAction Kind = EUMGPrototypeNavigationAction::Navigate;
		FString Payload;
		FUMGPrototypeTransitionData Transition;
		bool bResolved = false;
	};

	struct FConversionContext
	{
		explicit FConversionContext(FConversionState& InState)
			: State(InState)
		{
		}

		FConversionContext WithNodeTheme(const TSharedPtr<FDesignNode>& Node) const
		{
			FConversionContext Next(State);
			Next.ThemeContext = ThemeContext;
			if (Node.IsValid())
			{
				for (const auto& Pair : Node->Theme)
				{
					Next.ThemeContext.Add(Pair.Key, Pair.Value);
				}
			}
			return Next;
		}

		FConversionState& State;
		TMap<FString, FString> ThemeContext;
	};

	struct FRecursionGuard
	{
		explicit FRecursionGuard(int32& InDepth, const TCHAR* InName = TEXT(""))
			: Depth(InDepth)
			, bValid(InDepth < MaxTreeRecursionDepth)
		{
			if (bValid)
			{
				++Depth;
			}
			else
			{
				UE_LOG(LogDesign2UMG, Verbose, TEXT("%s: recursion depth exceeded (%d)"), InName, MaxTreeRecursionDepth);
				UE_LOG(LogDesign2UMG, Error, TEXT("Design tree nesting is too deep (over %d levels). A cyclic reference may exist."), MaxTreeRecursionDepth);
			}
		}

		~FRecursionGuard()
		{
			if (bValid)
				--Depth;
		}

		int32& Depth;
		bool bValid = false;
	};

	const TCHAR* ToSizeModeLogString(EDesignSizeMode Mode)
	{
		switch (Mode)
		{
		case EDesignSizeMode::Fixed: return TEXT("fixed");
		case EDesignSizeMode::FillContainer: return TEXT("fill");
		case EDesignSizeMode::FitContent: return TEXT("fit");
		default: return TEXT("unknown");
		}
	}

	bool HasVisibleFill(const FDesignFill& Fill)
	{
		return Fill.bEnabled && Fill.Type != EDesignFillType::None;
	}

	bool HasVisualFill(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() && (HasVisibleFill(Node->Visual.Fill) || Node->Visual.Fills.Num() > 0);
	}

	bool HasRenderableVisual(const TSharedPtr<FDesignNode>& Node)
	{
		return Design2UMGCore::SharedSemantics::HasRenderableVisual(Node);
	}

	struct FChildSizeSnapshot
	{
		TSharedPtr<FDesignNode> Child;
		FDesignSize RawWidth;
		FDesignSize RawHeight;
		FDesignSize EffectiveWidth;
		FDesignSize EffectiveHeight;
		bool bUsedExpandedRef = false;
	};

	bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node)
	{
		return Design2UMGCore::SharedSemantics::ShouldSkipNode(Node);
	}

	bool IsHexColorLiteral(const FString& ColorValue)
	{
		if (!ColorValue.StartsWith(TEXT("#")))
			return false;
		const int32 Len = ColorValue.Len();
		return Len == 4 || Len == 7 || Len == 9;
	}

	FString BuildHexColorWithAlpha(const FString& ColorValue, uint8 Alpha)
	{
		if (!IsHexColorLiteral(ColorValue))
			return FString();

		FColor Parsed = FColor::FromHex(ColorValue);
		Parsed.A = Alpha;
		return FString::Printf(TEXT("#%02X%02X%02X%02X"), Parsed.R, Parsed.G, Parsed.B, Parsed.A);
	}

	bool IsNeutralPlaceholderColor(const FString& ColorValue)
	{
		if (!IsHexColorLiteral(ColorValue))
			return true;

		const FColor Parsed = FColor::FromHex(ColorValue);
		const uint8 MaxChannel = FMath::Max3(Parsed.R, Parsed.G, Parsed.B);
		const uint8 MinChannel = FMath::Min3(Parsed.R, Parsed.G, Parsed.B);
		return (Parsed.A <= 0x20) || ((MaxChannel - MinChannel) <= 0x10);
	}

	bool HasWidgetStringProperty(const FUMGWidgetNode& Widget, const TCHAR* Key)
	{
		return Design2UMGWidgetNodeAccess::HasWidgetString(Widget, Key);
	}

	bool ShouldLockExplicitFitDimensionForFlow(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Node->Children.Num() == 0
			&& (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			&& !Node->Text.Content.IsEmpty();
	}

	bool TryApplyPlaceholderSurfaceTint(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FConversionVisualSettingsPayload& VisualSettings)
	{
		const UDesign2UMGSettings::FResolvedPlaceholderHatchStyle& Style = VisualSettings.PlaceholderHatchStyle;
		const bool bExpandedRefInstancePlaceholder = Design2UMGSourceMetadata::HasNodeMetadataFlag(Node, TEXT("design2umg.expanded_ref_instance"));
		const FString& DefaultPlaceholderFillColor =
			bExpandedRefInstancePlaceholder ? Style.RefFillColor : Style.BaseFillColor;
		const FString& DefaultPlaceholderOutlineColor =
			bExpandedRefInstancePlaceholder ? Style.RefStripeColor : Style.BaseStripeColor;
		const bool bIsLeafContainer =
			(Node->Type == EDesignNodeType::Frame
				|| Node->Type == EDesignNodeType::Group
				|| Node->Type == EDesignNodeType::Rectangle)
			&& Node->Children.Num() == 0;
		const bool bIsExplicitEmptySlotSurface =
			Design2UMGSourceMetadata::HasNodeMetadataFlag(Node, TEXT("pen.slot.empty"))
			|| Design2UMGSourceMetadata::HasNodeMetadataFlag(Node, TEXT("source.slot.empty"));
		const bool bUseSlotPlaceholderTint =
			bIsLeafContainer
			&& Node->Transform.Width.Value >= 24.0f
			&& Node->Transform.Height.Value >= 12.0f
			&& (Node->LayoutData.bSemanticPlaceholderSurface || bIsExplicitEmptySlotSurface);
		if (!bUseSlotPlaceholderTint)
			return false;

		const FString FillColor = IsHexColorLiteral(Node->Visual.Fill.SolidColor) ? Node->Visual.Fill.SolidColor : FString();
		const FString StrokeColor = IsHexColorLiteral(Node->Visual.Stroke.Fill.SolidColor) ? Node->Visual.Stroke.Fill.SolidColor : FString();
		const FString PlaceholderOutlineColor =
			(!StrokeColor.IsEmpty() && !IsNeutralPlaceholderColor(StrokeColor)) ? StrokeColor : DefaultPlaceholderOutlineColor;
		FString PlaceholderFillColor =
			(!FillColor.IsEmpty() && !IsNeutralPlaceholderColor(FillColor)) ? FillColor : DefaultPlaceholderFillColor;
		if (PlaceholderFillColor.IsEmpty() && !PlaceholderOutlineColor.IsEmpty())
			PlaceholderFillColor = BuildHexColorWithAlpha(PlaceholderOutlineColor, 0x33);
		if (PlaceholderFillColor.IsEmpty() || PlaceholderOutlineColor.IsEmpty())
			return false;

		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: applying placeholder surface tint to '%s'"), *Node->Name);
		Widget.Type = TEXT("Image");
		Widget.Children.Empty();
		const int32 InsetPx = FMath::Clamp(
			FMath::RoundToInt(FMath::Min3(10.0f, Node->Transform.Width.Value * 0.25f, Node->Transform.Height.Value * 0.25f)),
			1,
			10);
		Widget.Brush = FUMGVisualBrushData();
		Widget.Brush.MaterialRef = TEXT("placeholdersurface");
		Widget.Brush.bHasMaterialRef = true;
		Widget.Brush.MaterialParams = FString::Printf(
			TEXT("{\"Color\":\"%s\",\"StripeColor\":\"%s\",\"StripeWidth\":%d,\"StripeGap\":%d,\"Inset\":%d}"),
			*PlaceholderFillColor,
			*PlaceholderOutlineColor,
			Style.StripeWidthPx,
			Style.StripeGapPx,
			InsetPx);
		Widget.Brush.bHasMaterialParams = true;
		if (Node->Transform.Width.Value > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = Node->Transform.Width.Value;
		}
		if (Node->Transform.Height.Value > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = Node->Transform.Height.Value;
		}
		return true;
	}

	bool ShouldWrapNodeWithBorder(const TSharedPtr<FDesignNode>& Node, bool bPlaceholderUsesDirectImage)
	{
		const bool bNeedsPaddingWrapper =
			FLayoutMapper::HasPadding(Node)
			&& (Node->LayoutData.bHasPaddingRuntimeWrapDecision
				? Node->LayoutData.bPaddingRequiresRuntimeWrap
				: ((Node->LayoutData.Layout == EDesignLayout::None)
					|| ((Node->LayoutData.Layout == EDesignLayout::Wrap || Node->LayoutData.Layout == EDesignLayout::Grid)
						&& Node->Children.Num() > 0)));

		return (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
			&& (HasVisualFill(Node)
				|| bNeedsPaddingWrapper
				|| Node->Visual.CornerRadius.HasRadius()
				|| Node->Visual.Stroke.HasStroke())
			&& !bPlaceholderUsesDirectImage;
	}

	bool ShouldKeepStructuralLeafInFlow(const TSharedPtr<FDesignNode>& Node, EDesignLayout ParentLayout)
	{
		if (!Node.IsValid()
			|| (ParentLayout != EDesignLayout::Wrap && ParentLayout != EDesignLayout::Grid))
		{
			return false;
		}

		if (Node->Children.Num() > 0 || HasRenderableVisual(Node))
		{
			return false;
		}

		return (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
			&& Node->Transform.Width.Value > 0.0f
			&& Node->Transform.Height.Value > 0.0f;
	}

	void GatherVisibleChildrenForConversion(const TSharedPtr<FDesignNode>& Node, TArray<TSharedPtr<FDesignNode>>& OutVisibleChildren)
	{
		if (!Node.IsValid())
		{
			return;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (!ShouldSkipNode(Child) || ShouldKeepStructuralLeafInFlow(Child, Node->LayoutData.Layout))
			{
				OutVisibleChildren.Add(Child);
			}
		}
	}

	FUMGWidgetNode ApplyVisualEffectWrappers(
		FUMGWidgetNode Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& VisualMapperOptions)
	{
		if (Node->HasMultiFill())
			Widget = FVisualMapper::WrapWithMultiFill(MoveTemp(Widget), Node, VisualMapperOptions);

		const FDesignShadow* InnerShadow = nullptr;
		for (const FDesignEffect& Effect : Node->Visual.Effects)
		{
			if (!Effect.bEnabled || Effect.Type != EDesignEffectType::Shadow || !Effect.Shadow.bInner)
			{
				continue;
			}

			if (!InnerShadow || Effect.Shadow.Blur > InnerShadow->Blur)
			{
				InnerShadow = &Effect.Shadow;
			}
		}

		if (InnerShadow && Node.IsValid() && Node->Transform.Width.Value > 0.0f && Node->Transform.Height.Value > 0.0f)
		{
			Widget = FVisualMapper::WrapWithInnerShadow(MoveTemp(Widget), Node, *InnerShadow);
		}

		int32 ShadowCount = 0;
		ShadowCount = Widget.Shadow.BoxShadows.Num();
		if (ShadowCount > 0)
		{
			const TArray<FUMGBoxShadowLayerData> BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
			for (int32 Index = 0; Index < ShadowCount; ++Index)
			{
				Widget = FVisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, BoxShadows[Index], Index);
			}
		}

		float MaxForegroundBlur = 0.0f;
		float MaxBackgroundBlur = 0.0f;
		for (const FDesignEffect& Effect : Node->Visual.Effects)
		{
			if (!Effect.bEnabled || Effect.Radius <= 0.0f)
			{
				continue;
			}

			if (Effect.Type == EDesignEffectType::Blur)
			{
				MaxForegroundBlur = FMath::Max(MaxForegroundBlur, Effect.Radius);
			}
			else if (Effect.Type == EDesignEffectType::BackgroundBlur)
			{
				MaxBackgroundBlur = FMath::Max(MaxBackgroundBlur, Effect.Radius);
			}
		}

		if (MaxForegroundBlur > 0.0f && Node.IsValid() && Node->Transform.Width.Value > 0.0f && Node->Transform.Height.Value > 0.0f)
		{
			Widget = FVisualMapper::WrapWithForegroundBlur(MoveTemp(Widget), Node, MaxForegroundBlur);
		}

		if (MaxBackgroundBlur > 0.0f)
		{
			Widget = FVisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), MaxBackgroundBlur);
		}

		return Widget;
	}

	void WarmSharedTextNode(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context);

	struct FExpandedNodeResolver
	{
		explicit FExpandedNodeResolver(const FConversionContext& InContext)
			: Context(InContext)
		{
		}

		TSharedPtr<FDesignNode> ExpandRef(const TSharedPtr<FDesignNode>& Node) const
		{
			if (!Node.IsValid() || Node->Type != EDesignNodeType::Ref)
			{
				return Node;
			}

			TSharedPtr<FDesignNode> Expanded = FRefExpander::ExpandRef(Node, Context.State.Document);
			if (!Expanded.IsValid())
			{
				return nullptr;
			}

			if (Context.ThemeContext.Num() > 0)
			{
				FDesignVariableResolver Resolver(Context.State.Document.Variables);
				Resolver.SetThemeContext(Context.ThemeContext);
				FRefExpander::ReThemeNode(Expanded, Resolver);
			}

			return Expanded;
		}

		TSharedPtr<FDesignNode> ResolveExpandedChild(const TSharedPtr<FDesignNode>& Child, const bool bPreComputeFitContent) const
		{
			TSharedPtr<FDesignNode> ExpandedChild = ExpandRef(Child);
			if (!ExpandedChild.IsValid())
			{
				return Child;
			}

			if (bPreComputeFitContent && ExpandedChild != Child)
			{
				SizeEstimator::PreComputeFitContentSizes(ExpandedChild, Context.State.Document);
			}

			return ExpandedChild;
		}

		FChildSizeSnapshot BuildChildSizeSnapshot(const TSharedPtr<FDesignNode>& Child) const
		{
			FChildSizeSnapshot Snapshot;
			Snapshot.Child = Child;
			if (!Child.IsValid())
			{
				return Snapshot;
			}

			Snapshot.RawWidth = Child->Transform.Width;
			Snapshot.RawHeight = Child->Transform.Height;
			Snapshot.EffectiveWidth = Child->Transform.Width;
			Snapshot.EffectiveHeight = Child->Transform.Height;

			const TSharedPtr<FDesignNode> ExpandedChild = ExpandRef(Child);
			if (ExpandedChild != Child && ExpandedChild.IsValid())
			{
				Snapshot.bUsedExpandedRef = true;
				if (!Child->Transform.bHasWidth)
				{
					Snapshot.EffectiveWidth = ExpandedChild->Transform.Width;
				}
				if (!Child->Transform.bHasHeight)
				{
					Snapshot.EffectiveHeight = ExpandedChild->Transform.Height;
				}
			}

			return Snapshot;
		}

	private:
		const FConversionContext& Context;
	};

	struct FFlowChildPreparation
	{
		FFlowChildPreparation(
			const FConversionContext& InContext,
			const FExpandedNodeResolver& InExpandedNodeResolver)
			: Context(InContext)
			, ExpandedNodeResolver(InExpandedNodeResolver)
		{
		}

		void PreResolveFlowChildren(
			const TSharedPtr<FDesignNode>& Node,
			const FUMGWidgetNode& Widget,
			const TArray<TSharedPtr<FDesignNode>>& VisibleChildren) const
		{
			PreResolveLinearFlowFillChildren(Node, Widget, VisibleChildren);
			PreResolveFlowFitChildren(Node, VisibleChildren);
		}

	private:
		struct FFlowLayoutSizingContext
		{
			float InnerW = 0.0f;
			float InnerH = 0.0f;
			float TotalGap = 0.0f;
			bool bHorizontal = false;
		};

		struct FFlowChildSizingSummary
		{
			float FixedMainSum = 0.0f;
			int32 FillCount = 0;
		};

		static bool HasExplicitFitDimension(const FDesignSize& Size)
		{
			return Size.IsFit() && Size.Value > 0.0f;
		}

		static bool ShouldPreResolveLinearFlow(
			const TSharedPtr<FDesignNode>& Node,
			const FUMGWidgetNode& Widget)
		{
			return Node.IsValid()
				&& Widget.Bounds.W > 0.0f
				&& Widget.Bounds.H > 0.0f
				&& (Node->LayoutData.Layout == EDesignLayout::Horizontal || Node->LayoutData.Layout == EDesignLayout::Vertical);
		}

		FFlowLayoutSizingContext BuildFlowLayoutSizingContext(
			const TSharedPtr<FDesignNode>& Node,
			const FUMGWidgetNode& Widget,
			const int32 ChildCount) const
		{
			FFlowLayoutSizingContext SizingContext;
			SizingContext.InnerW = Widget.Bounds.W - Node->LayoutData.Padding.Left - Node->LayoutData.Padding.Right;
			SizingContext.InnerH = Widget.Bounds.H - Node->LayoutData.Padding.Top - Node->LayoutData.Padding.Bottom;
			SizingContext.bHorizontal = Node->LayoutData.Layout == EDesignLayout::Horizontal;
			SizingContext.TotalGap = ChildCount > 1 ? Node->LayoutData.Gap * (ChildCount - 1) : 0.0f;
			return SizingContext;
		}

		TArray<FChildSizeSnapshot> BuildFlowChildSnapshots(const TArray<TSharedPtr<FDesignNode>>& VisibleChildren) const
		{
			TArray<FChildSizeSnapshot> Snapshots;
			Snapshots.Reserve(VisibleChildren.Num());
			for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
			{
				Snapshots.Add(ExpandedNodeResolver.BuildChildSizeSnapshot(Child));
			}
			return Snapshots;
		}

		FFlowChildSizingSummary CollectFlowChildSizingSummary(
			const TArray<FChildSizeSnapshot>& ChildSnapshots,
			const bool bHorizontal) const
		{
			FFlowChildSizingSummary Summary;
			for (const FChildSizeSnapshot& ChildSnapshot : ChildSnapshots)
			{
				const FDesignSize& MainSize = bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
				if (MainSize.IsFill())
				{
					++Summary.FillCount;
				}
				else
				{
					Summary.FixedMainSum += MainSize.Value;
				}

				const FDesignSize& RawMain = bHorizontal ? ChildSnapshot.RawWidth : ChildSnapshot.RawHeight;
				const FDesignSize& RawCross = bHorizontal ? ChildSnapshot.RawHeight : ChildSnapshot.RawWidth;
				const FDesignSize& EffectiveCross = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
				UE_LOG(LogDesign2UMG, Verbose, TEXT("PreResolveFill Child '%s': ref=%d rawMain=%s(%.0f) effMain=%s(%.0f) rawCross=%s(%.0f) effCross=%s(%.0f)"),
					ChildSnapshot.Child.IsValid() ? *ChildSnapshot.Child->Id : TEXT("<invalid>"),
					ChildSnapshot.bUsedExpandedRef ? 1 : 0,
					ToSizeModeLogString(RawMain.Mode), RawMain.Value,
					ToSizeModeLogString(MainSize.Mode), MainSize.Value,
					ToSizeModeLogString(RawCross.Mode), RawCross.Value,
					ToSizeModeLogString(EffectiveCross.Mode), EffectiveCross.Value);
			}
			return Summary;
		}

		void ApplyFlowCrossFillSizing(
			FChildSizeSnapshot& ChildSnapshot,
			const FFlowLayoutSizingContext& SizingContext) const
		{
			const FDesignSize& CrossSize = SizingContext.bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
			if (CrossSize.IsFill())
			{
				SizeEstimator::SetResolvedNodeDimension(
					ChildSnapshot.Child,
					!SizingContext.bHorizontal,
					SizingContext.bHorizontal ? SizingContext.InnerH : SizingContext.InnerW);
			}
		}

		void ApplyFlowFillSizing(
			const FUMGWidgetNode& Widget,
			TArray<FChildSizeSnapshot>& ChildSnapshots,
			const FFlowLayoutSizingContext& SizingContext,
			const FFlowChildSizingSummary& Summary) const
		{
			if (Summary.FillCount <= 0)
			{
				for (FChildSizeSnapshot& ChildSnapshot : ChildSnapshots)
				{
					ApplyFlowCrossFillSizing(ChildSnapshot, SizingContext);
				}
				return;
			}

			const float AvailableMain =
				(SizingContext.bHorizontal ? SizingContext.InnerW : SizingContext.InnerH)
				- Summary.FixedMainSum
				- SizingContext.TotalGap;
			const float FillMain = FMath::Max(0.0f, AvailableMain / Summary.FillCount);
			for (FChildSizeSnapshot& ChildSnapshot : ChildSnapshots)
			{
				const FDesignSize& MainSize = SizingContext.bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
				if (MainSize.IsFill())
				{
					SizeEstimator::SetResolvedNodeDimension(ChildSnapshot.Child, SizingContext.bHorizontal, FillMain);
				}

				ApplyFlowCrossFillSizing(ChildSnapshot, SizingContext);
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFill: '%s' layout=%s inner=(%.0f,%.0f) fixed=%.0f fill=%d each=%.0f"),
				*Widget.Name,
				SizingContext.bHorizontal ? TEXT("H") : TEXT("V"),
				SizingContext.InnerW,
				SizingContext.InnerH,
				Summary.FixedMainSum,
				Summary.FillCount,
				FillMain);
		}

		void PreResolveLinearFlowFillChildren(
			const TSharedPtr<FDesignNode>& Node,
			const FUMGWidgetNode& Widget,
			const TArray<TSharedPtr<FDesignNode>>& VisibleChildren) const
		{
			if (!ShouldPreResolveLinearFlow(Node, Widget))
			{
				return;
			}

			TArray<FChildSizeSnapshot> ChildSnapshots = BuildFlowChildSnapshots(VisibleChildren);
			const FFlowLayoutSizingContext SizingContext =
				BuildFlowLayoutSizingContext(Node, Widget, ChildSnapshots.Num());
			const FFlowChildSizingSummary Summary =
				CollectFlowChildSizingSummary(ChildSnapshots, SizingContext.bHorizontal);
			ApplyFlowFillSizing(Widget, ChildSnapshots, SizingContext, Summary);
		}

		void PreResolveFlowFitChild(
			const TSharedPtr<FDesignNode>& ParentNode,
			const TSharedPtr<FDesignNode>& Child) const
		{
			if (!Child.IsValid() || (!Child->Transform.Width.IsFit() && !Child->Transform.Height.IsFit()))
			{
				return;
			}

			const bool bLockExplicitFit = ShouldLockExplicitFitDimensionForFlow(Child);
			const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(ParentNode, Child, 0.0f);
			const TSharedPtr<FDesignNode> EstimateChild = ExpandedNodeResolver.ResolveExpandedChild(Child, false);
			const FVector2D Estimate = SizeEstimator::EstimateNodeSize(EstimateChild, ChildContentWidth);
			if (!(bLockExplicitFit && HasExplicitFitDimension(Child->Transform.Width)))
			{
				SizeEstimator::PromoteFitNodeDimension(Child, true, Estimate.X);
			}
			if (!(bLockExplicitFit && HasExplicitFitDimension(Child->Transform.Height)))
			{
				SizeEstimator::PromoteFitNodeDimension(Child, false, Estimate.Y);
			}
			if (Estimate.X > 0.0f || Estimate.Y > 0.0f)
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFitContent: '%s' estimated=(%.0f,%.0f)"),
					*Child->Name, Estimate.X, Estimate.Y);
			}
		}

		void PreResolveFlowFitChildren(
			const TSharedPtr<FDesignNode>& Node,
			const TArray<TSharedPtr<FDesignNode>>& VisibleChildren) const
		{
			for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
			{
				PreResolveFlowFitChild(Node, Child);
			}
		}

		const FConversionContext& Context;
		const FExpandedNodeResolver& ExpandedNodeResolver;
	};

	struct FNodeConversionOrchestrator;

	struct FNodeChildAppender
	{
		FNodeChildAppender(
			FUMGWidgetNode& InWidget,
			const TSharedPtr<FDesignNode>& InNode,
			const TArray<TSharedPtr<FDesignNode>>& InVisibleChildren,
			const FConversionContext& InContext,
			const FExpandedNodeResolver& InExpandedNodeResolver,
			const FNodeConversionOrchestrator& InOrchestrator)
			: Widget(InWidget)
			, Node(InNode)
			, VisibleChildren(InVisibleChildren)
			, Context(InContext)
			, ExpandedNodeResolver(InExpandedNodeResolver)
			, Orchestrator(InOrchestrator)
		{
		}

		void AppendChildren() const;

	private:
		bool ShouldWrapChildWithSizeBox(
			const TSharedPtr<FDesignNode>& ChildNode,
			const FUMGWidgetNode& ChildWidget) const
		{
			const bool bHasFixedSize = ChildNode->Transform.Width.IsFixed() || ChildNode->Transform.Height.IsFixed();
			const bool bHasMinSize = (ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Width.Value > 0)
				|| (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Height.Value > 0);
			const bool bCanvasMixedAxis = Node->LayoutData.Layout == EDesignLayout::None
				&& ((ChildNode->Transform.Width.IsFit() && ChildNode->Transform.Height.IsFixed())
					|| (ChildNode->Transform.Height.IsFit() && ChildNode->Transform.Width.IsFixed()));
			const bool bWrapOrGridMeasuredFill =
				(Node->LayoutData.Layout == EDesignLayout::Wrap || Node->LayoutData.Layout == EDesignLayout::Grid)
				&& ((ChildNode->Transform.Width.IsFill() && ChildNode->Transform.Width.Value > 0.0f)
					|| (ChildNode->Transform.Height.IsFill() && ChildNode->Transform.Height.Value > 0.0f));
			const bool bWidgetOwnsDesiredSize =
				Node->LayoutData.Layout != EDesignLayout::None
				&& ChildWidget.Type == TEXT("Image")
				&& ChildNode->Children.Num() == 0
				&& (HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::WidthOverride)
					|| HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::HeightOverride))
				&& (HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::MaterialRef)
					|| HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::TexturePath)
					|| HasWidgetStringProperty(ChildWidget, UMGPropertyKeys::PathGeometry));
			return Node->LayoutData.Layout == EDesignLayout::None
				? bCanvasMixedAxis
				: (bHasFixedSize || bHasMinSize || bWrapOrGridMeasuredFill) && !bWidgetOwnsDesiredSize;
		}

		FUMGWidgetNode& Widget;
		const TSharedPtr<FDesignNode>& Node;
		const TArray<TSharedPtr<FDesignNode>>& VisibleChildren;
		const FConversionContext& Context;
		const FExpandedNodeResolver& ExpandedNodeResolver;
		const FNodeConversionOrchestrator& Orchestrator;
	};

	struct FNodeConversionOrchestrator
	{
		explicit FNodeConversionOrchestrator(const FConversionContext& InRootContext)
			: RootContext(InRootContext)
		{
		}

		FUMGWidgetNode Convert(const TSharedPtr<FDesignNode>& Node, const EDesignLayout ParentLayout = EDesignLayout::None) const
		{
			return ConvertNode(Node, RootContext, ParentLayout);
		}

		FUMGWidgetNode ConvertChild(
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout) const
		{
			return ConvertNode(Node, Context, ParentLayout);
		}

	private:
		FUMGWidgetNode ConvertNode(
			const TSharedPtr<FDesignNode>& Node,
			const FConversionContext& Context,
			const EDesignLayout ParentLayout) const
		{
			FRecursionGuard Guard(Context.State.RecursionDepth, TEXT("ConvertNode"));
			if (!Guard.bValid)
			{
				return FUMGWidgetNode();
			}

			if (ShouldSkipNode(Node) && !ShouldKeepStructuralLeafInFlow(Node, ParentLayout))
			{
				return FUMGWidgetNode();
			}

			const FConversionContext ActiveContext = Context.WithNodeTheme(Node);
			const FExpandedNodeResolver ExpandedNodeResolver(ActiveContext);
			const FFlowChildPreparation FlowChildPreparation(ActiveContext, ExpandedNodeResolver);

			if (Node->Type == EDesignNodeType::Ref)
			{
				TSharedPtr<FDesignNode> Expanded = ExpandedNodeResolver.ExpandRef(Node);
				if (!Expanded.IsValid())
				{
					return FUMGWidgetNode();
				}

				SizeEstimator::PreComputeFitContentSizes(Expanded, Context.State.Document);
				return ConvertNode(Expanded, ActiveContext, ParentLayout);
			}

			const bool bUsingRenderFallback = Design2UMGSourceMetadata::ShouldUseRenderFallback(Node);
			FUMGWidgetNode Widget = CreateBaseWidgetForNode(Node, Context.State);

			UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' id=%s type=%s bounds=(%.1f,%.1f,%.1f,%.1f) layout=%d children=%d"),
				*Widget.Name, *Widget.Id, *Widget.Type,
				Widget.Bounds.X, Widget.Bounds.Y, Widget.Bounds.W, Widget.Bounds.H,
				(int32)Node->LayoutData.Layout, Node->Children.Num());

			if (!bUsingRenderFallback)
			{
				FVisualMapper::ApplyVisualProperties(Widget, Node);
				if (Widget.Type == TEXT("Border")
					&& !HasWidgetStringProperty(Widget, UMGPropertyKeys::BrushColor)
					&& !HasWidgetStringProperty(Widget, UMGPropertyKeys::TexturePath)
					&& !HasWidgetStringProperty(Widget, UMGPropertyKeys::MaterialRef))
				{
					Widget.Brush.BrushColor = FLinearColor::Transparent;
					Widget.Brush.bHasBrushColor = true;
					if (HasWidgetStringProperty(Widget, UMGPropertyKeys::OutlineThickness))
					{
						Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
						Widget.Brush.bHasDrawAs = true;
					}
				}

				TArray<TSharedPtr<FDesignNode>> VisibleChildren;
				GatherVisibleChildrenForConversion(Node, VisibleChildren);
				if (Node->LayoutData.Layout == EDesignLayout::Grid && Design2UMGGridLayout::NeedsGridPanel(VisibleChildren))
				{
					Widget.Type = TEXT("GridPanel");
				}
				FlowChildPreparation.PreResolveFlowChildren(Node, Widget, VisibleChildren);
				FNodeChildAppender(Widget, Node, VisibleChildren, ActiveContext, ExpandedNodeResolver, *this).AppendChildren();

				const bool bHasResolvedGridGap = Node->LayoutData.Layout == EDesignLayout::Grid
					&& (Design2UMGGridLayout::ResolveGridColumnGap(Node) > KINDA_SMALL_NUMBER
						|| Design2UMGGridLayout::ResolveGridRowGap(Node) > KINDA_SMALL_NUMBER);
				if (Node->LayoutData.Gap > 0 || bHasResolvedGridGap)
				{
					if (Node->LayoutData.Layout == EDesignLayout::Wrap)
					{
						const float CrossAxisGap = Design2UMGSourceMetadata::ResolveWrapCrossAxisGap(Node);
						Widget.WidgetStyle.InnerSlotPadding = FVector2D(Node->LayoutData.Gap, CrossAxisGap);
					}
					else if (Node->LayoutData.Layout == EDesignLayout::Grid)
					{
						if (Widget.Type == TEXT("UniformGridPanel"))
						{
							const float ColumnGap = Design2UMGGridLayout::ResolveGridColumnGap(Node);
							const float RowGap = Design2UMGGridLayout::ResolveGridRowGap(Node);
							if (ColumnGap > KINDA_SMALL_NUMBER || RowGap > KINDA_SMALL_NUMBER)
							{
								Widget.WidgetStyle.SlotPadding =
									FMargin(ColumnGap * 0.5f, RowGap * 0.5f, ColumnGap * 0.5f, RowGap * 0.5f);
							}
							else
							{
								Widget.WidgetStyle.SlotPadding.Reset();
							}
						}
					}
					else
					{
						FLayoutMapper::ApplyGap(Widget.Children, Node->LayoutData.Gap, Node->LayoutData.Layout);
					}

					if (Node->LayoutData.Layout == EDesignLayout::Grid
						&& Widget.Type == TEXT("UniformGridPanel")
						&& !Widget.WidgetStyle.SlotPadding.IsSet())
					{
						Widget.WidgetStyle.SlotPadding = FMargin(Node->LayoutData.Gap, Node->LayoutData.Gap, Node->LayoutData.Gap, Node->LayoutData.Gap);
					}
				}

				if (Node->LayoutData.JustifyContent != EDesignJustifyContent::Start)
				{
					const EDesignSizeMode MainAxisMode = (Node->LayoutData.Layout == EDesignLayout::Horizontal)
						? Node->Transform.Width.Mode : Node->Transform.Height.Mode;
					FLayoutMapper::ApplyJustifyContent(Widget.Children, Node->LayoutData.JustifyContent, MainAxisMode, ParentLayout);
				}

				const bool bPlaceholderUsesDirectImage = TryApplyPlaceholderSurfaceTint(Widget, Node, Context.State.SettingsSnapshot.Visual);
				if (ShouldWrapNodeWithBorder(Node, bPlaceholderUsesDirectImage))
				{
					UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
						*Widget.Name, HasVisualFill(Node), Node->Visual.CornerRadius.HasRadius(), Node->Visual.Stroke.HasStroke());
					Widget = FVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
				}
				else if (FLayoutMapper::HasPadding(Node) && !bPlaceholderUsesDirectImage)
				{
					FLayoutMapper::ApplyPadding(Widget, Node->LayoutData.Padding);
				}

				Widget = ApplyVisualEffectWrappers(MoveTemp(Widget), Node, Context.State.SettingsSnapshot.Visual.VisualMapperOptions);
				if (Design2UMGPrototypeNavigation::ShouldWrapNodeWithButton(
					Node,
					Context.State.PrototypeNavigation,
					Context.State.PrototypeTriggerNodeIds))
				{
					Widget = Design2UMGPrototypeNavigation::WrapWithButton(MoveTemp(Widget), Node);
				}
			}
			else
			{
				FVisualMapper::ApplyRenderTransformProperties(Widget, Node);
			}
			Widget.MutableSource() = Design2UMGSourceMetadata::BuildSourceNodeData(Node);

			return Widget;
		}

		void ApplyRenderFallbackBounds(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node) const
		{
			if (!Node.IsValid())
			{
				return;
			}

			const FString& FallbackOffsetX = Node->Source().RenderFallback.OffsetX;
			const FString& FallbackOffsetY = Node->Source().RenderFallback.OffsetY;
			const FString& FallbackWidth = Node->Source().RenderFallback.Width;
			const FString& FallbackHeight = Node->Source().RenderFallback.Height;
			if (!FallbackOffsetX.IsEmpty())
			{
				Widget.Bounds.X += FCString::Atof(*FallbackOffsetX);
			}
			if (!FallbackOffsetY.IsEmpty())
			{
				Widget.Bounds.Y += FCString::Atof(*FallbackOffsetY);
			}
			if (!FallbackWidth.IsEmpty())
			{
				Widget.Bounds.W = FCString::Atof(*FallbackWidth);
			}
			if (!FallbackHeight.IsEmpty())
			{
				Widget.Bounds.H = FCString::Atof(*FallbackHeight);
			}
		}

		FUMGWidgetNode CreateBaseWidgetForNode(const TSharedPtr<FDesignNode>& Node, FConversionState& State) const
		{
			if (Design2UMGSourceMetadata::ShouldUseRenderFallback(Node))
			{
				FUMGWidgetNode Widget = Design2UMGSourceMetadata::CreateRenderFallbackWidget(Node);
				Design2UMGNodeWidgetFactory::PopulateWidgetIdentityAndBounds(Widget, Node);
				ApplyRenderFallbackBounds(Widget, Node);
				Widget.MutableSource() = Design2UMGSourceMetadata::BuildSourceNodeData(Node);
				UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' -> Figma render fallback image"), *Widget.Name);
				return Widget;
			}

			Design2UMGNodeWidgetFactory::FBuildArgs BuildArgs;
			BuildArgs.ExistingWarnedFamilies = &State.WarnedFamilies;
			BuildArgs.IconFontSettingsSnapshot = &State.SettingsSnapshot.IconFont;

			Design2UMGNodeWidgetFactory::FBuildResult BuildResult =
				Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(Node, BuildArgs);
			State.Warnings.Append(BuildResult.Warnings);
			for (const FString& WarnedFamily : BuildResult.NewlyWarnedFamilies)
			{
				State.WarnedFamilies.Add(WarnedFamily);
			}
			for (const FString& PrunedFamily : BuildResult.PrunedInvalidMappings)
			{
				State.PrunedInvalidMappings.Add(PrunedFamily);
			}
			for (const TPair<FString, FString>& PendingMapping : BuildResult.PendingIconFontMappings)
			{
				State.PendingIconFontMappings.Add(PendingMapping.Key, PendingMapping.Value);
			}

			FUMGWidgetNode Widget = MoveTemp(BuildResult.Widget);
			Design2UMGNodeWidgetFactory::PopulateWidgetIdentityAndBounds(Widget, Node);
			return Widget;
		}

		const FConversionContext& RootContext;
	};

	void WarmSharedTextCalibrationRecursive(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context)
	{
		if (!Node.IsValid() || ShouldSkipNode(Node))
		{
			return;
		}

		const FConversionContext ActiveContext = Context.WithNodeTheme(Node);
		if (Node->Type == EDesignNodeType::Ref)
		{
			const FExpandedNodeResolver ExpandedNodeResolver(ActiveContext);
			const TSharedPtr<FDesignNode> ActiveNode = ExpandedNodeResolver.ExpandRef(Node);
			if (!ActiveNode.IsValid() || ShouldSkipNode(ActiveNode))
			{
				return;
			}

			WarmSharedTextNode(ActiveNode, ActiveContext.WithNodeTheme(ActiveNode));
			return;
		}

		WarmSharedTextNode(Node, ActiveContext);
	}

	void WarmSharedTextNode(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context)
	{
		if (!Node.IsValid() || ShouldSkipNode(Node))
		{
			return;
		}

		if (Node->Type == EDesignNodeType::Text
			&& Node->Text.TextAlignVertical != EDesignTextAlignVertical::Top
			&& !Node->Text.Content.Contains(TEXT("\n"))
			&& TextMeasurement::IsWrappingTextMode(Node))
		{
			(void)TextMeasurement::ResolveTextFontSizeForUMG(Node);
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			WarmSharedTextCalibrationRecursive(Child, Context);
		}
	}

	void WarmSharedTextCalibration(
		const TArray<TSharedPtr<FDesignNode>>& Nodes,
		const FConversionContext& Context)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			WarmSharedTextCalibrationRecursive(Node, Context);
		}
	}

	void FNodeChildAppender::AppendChildren() const
	{
		const bool bGridPanelParent =
			Node.IsValid()
			&& Node->LayoutData.Layout == EDesignLayout::Grid
			&& Widget.Type.Equals(TEXT("GridPanel"), ESearchCase::CaseSensitive);
		Design2UMGGridLayout::FGridPlacementState GridPlacementState;
		if (bGridPanelParent)
		{
			GridPlacementState.ColumnCount = Design2UMGGridLayout::ResolveGridColumnCount(Node, VisibleChildren);
			GridPlacementState.RowCount = Design2UMGGridLayout::ResolveGridRowCount(Node, VisibleChildren, GridPlacementState.ColumnCount);
		}

		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
		{
			TSharedPtr<FDesignNode> EffectiveChild = ExpandedNodeResolver.ResolveExpandedChild(Child, true);
			FUMGWidgetNode ChildWidget = Orchestrator.ConvertChild(EffectiveChild, Context, Node->LayoutData.Layout);
			if (ChildWidget.Type.IsEmpty())
			{
				continue;
			}

			FLayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->LayoutData.Layout);
			FLayoutMapper::ApplyAlignItems(ChildWidget, Node->LayoutData.AlignItems, Node->LayoutData.Layout);
			if (Node->LayoutData.Layout == EDesignLayout::Grid)
			{
				if (bGridPanelParent)
				{
					Design2UMGGridLayout::ApplyGridPanelChildSlotData(ChildWidget, Node, EffectiveChild, GridPlacementState);
				}
				else
				{
					if (EffectiveChild->LayoutData.HasLayoutRowValue())
					{
						ChildWidget.Slot.Row = FMath::Max(0, EffectiveChild->LayoutData.LayoutRow);
					}
					if (EffectiveChild->LayoutData.HasLayoutColumnValue())
					{
						ChildWidget.Slot.Column = FMath::Max(0, EffectiveChild->LayoutData.LayoutColumn);
					}
					Design2UMGGridLayout::ApplyGridChildAlignmentOverrides(ChildWidget, Node, EffectiveChild);
				}
			}
			if (Node->LayoutData.Layout != EDesignLayout::None && !EffectiveChild->LayoutData.bAbsoluteLayoutPositioning)
			{
				ChildWidget.Bounds.X = 0.0f;
				ChildWidget.Bounds.Y = 0.0f;
			}

			if (ShouldWrapChildWithSizeBox(EffectiveChild, ChildWidget))
			{
				const bool bHasFixedSize = EffectiveChild->Transform.Width.IsFixed() || EffectiveChild->Transform.Height.IsFixed();
				const bool bHasMinSize = (EffectiveChild->Transform.Width.IsFit() && EffectiveChild->Transform.Width.Value > 0)
					|| (EffectiveChild->Transform.Height.IsFit() && EffectiveChild->Transform.Height.Value > 0);
				UE_LOG(LogDesign2UMG, Verbose, TEXT("  child '%s' → SizeBox wrap (fixed=%d min=%d w=%.1f h=%.1f)"),
					*ChildWidget.Name, bHasFixedSize, bHasMinSize,
					EffectiveChild->Transform.Width.Value, EffectiveChild->Transform.Height.Value);
				Widget.Children.Add(FLayoutMapper::WrapWithSizeBox(MoveTemp(ChildWidget), EffectiveChild, Node->LayoutData.Layout));
				continue;
			}

			Widget.Children.Add(MoveTemp(ChildWidget));
		}
	}
}

FUMGWidgetData FDesign2UMGConverter::Convert(const FDesignDocument& Document)
{
	FUMGWidgetData Result;

	for (const FDesignSkippedNodeReport& Report : Document.ParseDiagnostics().SkippedDesignerNodes)
	{
		Result.Warnings.AddUnique(FString::Printf(
			TEXT("Figma %s '%s' (%s) skipped from runtime widget tree [%s]"),
			*Report.NodeType,
			Report.NodeName.IsEmpty() ? TEXT("<unnamed>") : *Report.NodeName,
			Report.NodeId.IsEmpty() ? TEXT("<no-id>") : *Report.NodeId,
			Report.Reason.IsEmpty() ? TEXT("designer_only") : *Report.Reason));
	}

	const FConversionSettingsSnapshot SettingsSnapshot = BuildConversionSettingsSnapshot();
	FConversionState State(Document, Result.Warnings, SettingsSnapshot);
	FConversionContext RootContext(State);
	const FNodeConversionOrchestrator NodeOrchestrator(RootContext);

	if (!Document.SourceDocument().ComponentsJson.IsEmpty() || !Document.SourceDocument().ComponentSetsJson.IsEmpty())
	{
		FUMGSourceDocumentData& SourceDocument = Result.MutableSourceDocument();
		SourceDocument.ComponentsJson = Document.SourceDocument().ComponentsJson;
		SourceDocument.ComponentSetsJson = Document.SourceDocument().ComponentSetsJson;
	}

	TextMeasurement::ResetCache();

	// Deep-copy children so conversion never mutates the caller's source tree through shared refs.
	TArray<TSharedPtr<FDesignNode>> Children;
	Children.Reserve(Document.Children.Num());
	for (const auto& Child : Document.Children)
		Children.Add(FRefExpander::DeepCopyNode(Child));

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	VisibleChildren.Reserve(Children.Num());
	for (const auto& Child : Children)
	{
		if (!ShouldSkipNode(Child))
		{
			VisibleChildren.Add(Child);
		}
	}

	{
		const Design2UMGPrototypeNavigation::FPrototypeNavigationAnalysis PrototypeAnalysis =
			Design2UMGPrototypeNavigation::BuildAnalysis(Document, VisibleChildren, Result.Warnings);
		State.PrototypeNavigation = PrototypeAnalysis.Data;
		State.PrototypeTriggerNodeIds = PrototypeAnalysis.TriggerNodeIds;
		if (PrototypeAnalysis.Data.IsValid())
		{
			Result.MutablePrototypeNavigation() = PrototypeAnalysis.Data;
		}
	}

	// Use the first non-skipped frame name for WidgetName, then fall back to WBP_FromPen.
	for (const auto& Child : VisibleChildren)
	{
		if (!Child->bReusable && !Child->Name.IsEmpty())
		{
			Result.WidgetName = FString::Printf(TEXT("WBP_%s"), *Child->Name);
			break;
		}
	}
	if (Result.WidgetName.IsEmpty())
		Result.WidgetName = TEXT("WBP_FromPen");

	// Pre-compute fit-content node sizes by recursively resolving child-driven bounds.
	for (const auto& Child : VisibleChildren)
	{
		SizeEstimator::PreComputeFitContentSizes(Child, Document);
	}

	TArray<FUMGWidgetNode> RenderWidgets;
	for (const auto& Child : VisibleChildren)
	{
		if (Child->bReusable)
			continue;

		Child->Transform.X = 0.0f;
		Child->Transform.Y = 0.0f;

		FUMGWidgetNode ChildWidget = NodeOrchestrator.Convert(Child);
		if (!ChildWidget.Type.IsEmpty())
			RenderWidgets.Add(MoveTemp(ChildWidget));
	}

	if (State.PrototypeNavigation.IsValid())
	{
		UE_LOG(LogDesign2UMG, Log,
			TEXT("Convert: %d top-level widgets → prototype root with %d navigation bindings"),
			RenderWidgets.Num(),
			State.PrototypeNavigation.ClickBindings.Num());
		Result.RootWidget = Design2UMGPrototypeNavigation::BuildRoot(MoveTemp(RenderWidgets), State.PrototypeNavigation);
		Result.IconFontMappingChanges.Upserts = MoveTemp(State.PendingIconFontMappings);
		Result.IconFontMappingChanges.Removals = MoveTemp(State.PrunedInvalidMappings);
		return Result;
	}

	if (RenderWidgets.Num() == 1)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Convert: single root '%s' type=%s"), *RenderWidgets[0].Name, *RenderWidgets[0].Type);
		Result.RootWidget = MoveTemp(RenderWidgets[0]);
		Result.IconFontMappingChanges.Upserts = MoveTemp(State.PendingIconFontMappings);
		Result.IconFontMappingChanges.Removals = MoveTemp(State.PrunedInvalidMappings);
		return Result;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("Convert: %d top-level widgets → CanvasPanel root"), RenderWidgets.Num());
	Result.RootWidget.Type = TEXT("CanvasPanel");
	Result.RootWidget.Name = TEXT("Root");
	Result.RootWidget.Id = TEXT("Root");
	for (auto& W : RenderWidgets)
		Result.RootWidget.Children.Add(MoveTemp(W));

	Result.IconFontMappingChanges.Upserts = MoveTemp(State.PendingIconFontMappings);
	Result.IconFontMappingChanges.Removals = MoveTemp(State.PrunedInvalidMappings);
	return Result;
}

