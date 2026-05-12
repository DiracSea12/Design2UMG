// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "RefExpander.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "Design2UMGLog.h"

namespace
{
	const TCHAR* ExpandedRefInstanceMetadataKey = TEXT("design2umg.expanded_ref_instance");

	bool ShouldApplySizeOverride(const FDesignSize& Size, bool bHasExplicitFlag)
	{
		// Explicit width/height always wins.
		if (bHasExplicitFlag)
			return true;

		// Pre-computed fit_content size (value > 0) should also propagate for ref instances.
		if (Size.Mode == EDesignSizeMode::FitContent)
			return Size.Value > 0.0f;

		// Non-fit modes (fixed/fill) are meaningful overrides even when explicit flags are absent.
		return true;
	}

	void ApplyOverrideProperties(FDesignNode& Target, const FDesignNode& Override)
	{
		const bool bFillExplicitClear = Override.Visual.Fill.bExplicitClear;
		const bool bKeepExplicitStrokeFill = Override.Visual.Stroke.Fill.IsSet();

		if (!Override.Text.Content.IsEmpty())
			Target.Text.Content = Override.Text.Content;
		if (Override.Text.FontSize > 0)
			Target.Text.FontSize = Override.Text.FontSize;
		if (!Override.Text.FontWeight.IsEmpty())
			Target.Text.FontWeight = Override.Text.FontWeight;
		if (!Override.Text.FontStyle.IsEmpty())
			Target.Text.FontStyle = Override.Text.FontStyle;
		if (!Override.Text.FontFamily.IsEmpty())
			Target.Text.FontFamily = Override.Text.FontFamily;
		if (Override.Visual.Fill.Type != EDesignFillType::None || Override.Visual.Fill.bExplicitClear)
		{
			Target.Visual.Fill = Override.Visual.Fill;
			Target.Visual.Fills = Override.Visual.Fills;
		}
		if (Override.Visual.Stroke.HasStroke())
			Target.Visual.Stroke = Override.Visual.Stroke;
		if (bFillExplicitClear && !bKeepExplicitStrokeFill)
			Target.Visual.Stroke.Fill.Reset();
		if (Override.Visual.bEffectsExplicitClear || Override.Visual.Effects.Num() > 0)
		{
			Target.Visual.Effects = Override.Visual.Effects;
			Target.Visual.bEffectsExplicitClear = Override.Visual.bEffectsExplicitClear;
		}
		if (Override.Transform.bHasWidth)
			Target.Transform.Width = Override.Transform.Width;
		if (Override.Transform.bHasHeight)
			Target.Transform.Height = Override.Transform.Height;
		if (Override.Transform.HasExplicitX())
		{
			Target.Transform.X = Override.Transform.X;
			Target.Transform.ExplicitX = Override.Transform.X;
		}
		if (Override.Transform.HasExplicitY())
		{
			Target.Transform.Y = Override.Transform.Y;
			Target.Transform.ExplicitY = Override.Transform.Y;
		}
		if (!Override.Transform.bEnabled)
			Target.Transform.bEnabled = false;
		if (Override.LayoutData.HasExplicitPadding())
		{
			Target.LayoutData.ExplicitPadding = Override.LayoutData.ExplicitPadding;
		}
		if (Override.LayoutData.HasExplicitGap())
		{
			Target.LayoutData.ExplicitGap = Override.LayoutData.ExplicitGap;
		}
		if (Override.LayoutData.HasExplicitClip())
		{
			Target.LayoutData.ExplicitClip = Override.LayoutData.ExplicitClip;
		}
		if (Override.Transform.HasExplicitOpacity())
		{
			Target.Transform.Opacity = Override.Transform.Opacity;
			Target.Transform.ExplicitOpacity = Override.Transform.Opacity;
		}
		if (Override.LayoutData.JustifyContent != EDesignJustifyContent::Start)
			Target.LayoutData.JustifyContent = Override.LayoutData.JustifyContent;
		if (Override.LayoutData.AlignItems != EDesignAlignItems::Start)
			Target.LayoutData.AlignItems = Override.LayoutData.AlignItems;
		if (Override.Text.TextAlign != EDesignTextAlign::Left)
			Target.Text.TextAlign = Override.Text.TextAlign;
		if (Override.Text.LineHeight > 0)
			Target.Text.LineHeight = Override.Text.LineHeight;
		if (Override.Text.TextGrowth != EDesignTextGrowth::Auto)
			Target.Text.TextGrowth = Override.Text.TextGrowth;
		if (Override.Text.bUnderline)
			Target.Text.bUnderline = true;
		if (Override.Text.bStrikethrough)
			Target.Text.bStrikethrough = true;
		if (!Override.Text.Href.IsEmpty())
			Target.Text.Href = Override.Text.Href;
		if (Override.Children.Num() > 0)
			Target.Children = Override.Children;
	}

	TSharedPtr<FDesignNode> MergeDescendantOverrideNodes(
		const TSharedPtr<FDesignNode>& BaseOverride,
		const TSharedPtr<FDesignNode>& OuterOverride)
	{
		if (!OuterOverride.IsValid())
			return BaseOverride;
		if (!BaseOverride.IsValid())
			return RefExpander::DeepCopyNode(OuterOverride);
		if (OuterOverride->Type != EDesignNodeType::Unknown)
			return RefExpander::DeepCopyNode(OuterOverride);

		auto Merged = RefExpander::DeepCopyNode(BaseOverride);
		if (!Merged.IsValid())
			return RefExpander::DeepCopyNode(OuterOverride);

		ApplyOverrideProperties(*Merged, *OuterOverride);
		return Merged;
	}

	void MergeDescendants(
		TMap<FString, TSharedPtr<FDesignNode>>& InOut,
		const TMap<FString, TSharedPtr<FDesignNode>>& Outer)
	{
		for (const auto& Pair : Outer)
		{
			TSharedPtr<FDesignNode> Existing = InOut.FindRef(Pair.Key);
			InOut.Add(Pair.Key, MergeDescendantOverrideNodes(Existing, Pair.Value));
		}
	}

	bool ApplyOverrideToDirectChild(
		TSharedPtr<FDesignNode>& Parent,
		const FString& TargetId,
		const TSharedPtr<FDesignNode>& Override)
	{
		if (!Parent.IsValid() || !Override.IsValid())
			return false;

		for (int32 i = 0; i < Parent->Children.Num(); ++i)
		{
			auto& Child = Parent->Children[i];
			if (!Child.IsValid() || Child->Id != TargetId)
				continue;

			if (Override->Type != EDesignNodeType::Unknown)
			{
				Parent->Children[i] = RefExpander::DeepCopyNode(Override);
				return true;
			}

			ApplyOverrideProperties(*Child, *Override);
			return true;
		}

		return false;
	}

	bool ApplyOverrideByIdRecursive(
		TSharedPtr<FDesignNode>& Node,
		const FString& TargetId,
		const TSharedPtr<FDesignNode>& Override,
		int32 Depth = 0)
	{
		constexpr int32 MaxDepth = 100;
		if (Depth > MaxDepth)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ApplyOverrideByIdRecursive: max depth %d exceeded for target '%s'"), MaxDepth, *TargetId);
			return false;
		}

		if (!Node.IsValid() || !Override.IsValid())
			return false;

		if (ApplyOverrideToDirectChild(Node, TargetId, Override))
			return true;

		// descendants key may be a bare node id instead of a slash path.
		for (auto& Child : Node->Children)
		{
			if (ApplyOverrideByIdRecursive(Child, TargetId, Override, Depth + 1))
				return true;
		}

		return false;
	}

	void MarkExpandedRefInstanceRecursive(TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return;
		}

		Node->MutableSource().bExpandedRefInstance = true;
		Node->Metadata.Add(ExpandedRefInstanceMetadataKey, TEXT("true"));
		for (TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			MarkExpandedRefInstanceRecursive(Child);
		}
	}

	void ApplyInstanceOverrides(TSharedPtr<FDesignNode>& Expanded, const TSharedPtr<FDesignNode>& RefNode)
	{
		if (!Expanded.IsValid() || !RefNode.IsValid())
			return;

		const bool bFillExplicitClear = RefNode->Visual.Fill.bExplicitClear;
		const bool bKeepExplicitStrokeFill = RefNode->Visual.Stroke.Fill.IsSet();

		Expanded->Id = RefNode->Id;
		if (!RefNode->Name.IsEmpty())
			Expanded->Name = RefNode->Name;

		if (RefNode->Transform.HasExplicitX())
		{
			Expanded->Transform.X = RefNode->Transform.X;
			Expanded->Transform.ExplicitX = RefNode->Transform.X;
		}
		else
		{
			// Instance with no explicit x should use local origin, not component definition canvas coordinate.
			Expanded->Transform.X = 0.0f;
			Expanded->Transform.ExplicitX.Reset();
		}
		if (RefNode->Transform.HasExplicitY())
		{
			Expanded->Transform.Y = RefNode->Transform.Y;
			Expanded->Transform.ExplicitY = RefNode->Transform.Y;
		}
		else
		{
			// Instance with no explicit y should use local origin, not component definition canvas coordinate.
			Expanded->Transform.Y = 0.0f;
			Expanded->Transform.ExplicitY.Reset();
		}
		if (ShouldApplySizeOverride(RefNode->Transform.Width, RefNode->Transform.bHasWidth))
		{
			Expanded->Transform.Width = RefNode->Transform.Width;
			Expanded->Transform.bHasWidth = true;
		}
		if (ShouldApplySizeOverride(RefNode->Transform.Height, RefNode->Transform.bHasHeight))
		{
			Expanded->Transform.Height = RefNode->Transform.Height;
			Expanded->Transform.bHasHeight = true;
		}

		if (RefNode->Visual.Fill.Type != EDesignFillType::None || RefNode->Visual.Fill.bExplicitClear)
		{
			Expanded->Visual.Fill = RefNode->Visual.Fill;
			Expanded->Visual.Fills = RefNode->Visual.Fills;
		}
		if (RefNode->Visual.Stroke.HasStroke())
			Expanded->Visual.Stroke = RefNode->Visual.Stroke;
		if (bFillExplicitClear && !bKeepExplicitStrokeFill)
			Expanded->Visual.Stroke.Fill.Reset();
		if (RefNode->Visual.bEffectsExplicitClear || RefNode->Visual.Effects.Num() > 0)
		{
			Expanded->Visual.Effects = RefNode->Visual.Effects;
			Expanded->Visual.bEffectsExplicitClear = RefNode->Visual.bEffectsExplicitClear;
		}
		if (RefNode->LayoutData.HasExplicitPadding())
		{
			Expanded->LayoutData.ExplicitPadding = RefNode->LayoutData.ExplicitPadding;
		}
		if (RefNode->LayoutData.HasExplicitGap())
		{
			Expanded->LayoutData.ExplicitGap = RefNode->LayoutData.ExplicitGap;
		}
		if (RefNode->LayoutData.HasExplicitClip())
		{
			Expanded->LayoutData.ExplicitClip = RefNode->LayoutData.ExplicitClip;
		}
		if (!RefNode->Transform.bEnabled)
			Expanded->Transform.bEnabled = false;
		if (RefNode->Transform.HasExplicitOpacity())
		{
			Expanded->Transform.Opacity = RefNode->Transform.Opacity;
			Expanded->Transform.ExplicitOpacity = RefNode->Transform.Opacity;
		}
		if (!FMath::IsNearlyZero(RefNode->Transform.Rotation))
			Expanded->Transform.Rotation = RefNode->Transform.Rotation;
		if (RefNode->Transform.bFlipX)
			Expanded->Transform.bFlipX = RefNode->Transform.bFlipX;
		if (RefNode->Transform.bFlipY)
			Expanded->Transform.bFlipY = RefNode->Transform.bFlipY;

		// Slot override has the highest priority.
		if (RefNode->Children.Num() > 0)
			Expanded->Children = RefNode->Children;
	}

	TSharedPtr<FDesignNode> ExpandRefInternal(
		const TSharedPtr<FDesignNode>& RefNode,
		const FDesignDocument& Doc,
		TSet<FString>& RefChain)
	{
		if (!RefNode.IsValid() || RefNode->Ref.IsEmpty())
			return nullptr;

		const FString RefTargetId = RefNode->Ref;
		if (RefChain.Contains(RefTargetId))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("ExpandRef: circular ref detected at '%s' -> '%s'"),
				*RefNode->Id, *RefTargetId);
			return nullptr;
		}

		RefChain.Add(RefTargetId);

		auto SourceNode = Doc.FindReusableNode(RefTargetId);
		if (!SourceNode.IsValid())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ExpandRef: ref '%s' -> '%s' NOT FOUND"), *RefNode->Id, *RefTargetId);
			RefChain.Remove(RefTargetId);
			return nullptr;
		}

		auto Expanded = RefExpander::DeepCopyNode(SourceNode);
		if (!Expanded.IsValid())
		{
			RefChain.Remove(RefTargetId);
			return nullptr;
		}
		MergeDescendants(Expanded->Descendants, RefNode->Descendants);
		ApplyInstanceOverrides(Expanded, RefNode);

		TSharedPtr<FDesignNode> Result = Expanded;
		if (Expanded->Type == EDesignNodeType::Ref && !Expanded->Ref.IsEmpty())
		{
			Result = ExpandRefInternal(Expanded, Doc, RefChain);
		}
		else
		{
			for (const auto& Pair : Expanded->Descendants)
				RefExpander::ApplyDescendantOverride(Result, Pair.Key, Pair.Value);
		}

		if (Result.IsValid() && !RefNode->bReusable)
		{
			MarkExpandedRefInstanceRecursive(Result);
		}

		RefChain.Remove(RefTargetId);
		return Result;
	}
}

TSharedPtr<FDesignNode> RefExpander::DeepCopyNode(const TSharedPtr<FDesignNode>& Source)
{
	if (!Source.IsValid())
		return nullptr;

	auto Copy = MakeShared<FDesignNode>();
	*Copy = *Source;

	// Deep copy children
	Copy->Children.Empty();
	for (const auto& Child : Source->Children)
		Copy->Children.Add(DeepCopyNode(Child));

	// Deep copy descendants to avoid cross-instance mutation.
	for (auto& Pair : Copy->Descendants)
		Pair.Value = DeepCopyNode(Pair.Value);

	return Copy;
}

TSharedPtr<FDesignNode> RefExpander::ExpandRef(const TSharedPtr<FDesignNode>& RefNode, const FDesignDocument& Doc)
{
	TSet<FString> RefChain;
	return ExpandRefInternal(RefNode, Doc, RefChain);
}

void RefExpander::ApplyDescendantOverride(TSharedPtr<FDesignNode>& Node, const FString& Path, const TSharedPtr<FDesignNode>& Override)
{
	if (!Node.IsValid() || !Override.IsValid())
		return;

	FString FirstPart, Remaining;
	if (Path.Split(TEXT("/"), &FirstPart, &Remaining))
	{
		for (auto& Child : Node->Children)
		{
			if (Child.IsValid() && Child->Id == FirstPart)
			{
				ApplyDescendantOverride(Child, Remaining, Override);
				return;
			}
		}
	}
	else
	{
		ApplyOverrideByIdRecursive(Node, Path, Override);
	}
}

void RefExpander::ReThemeNode(TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver)
{
	if (!Node.IsValid())
		return;

	// Primary fill
	if (!Node->Visual.Fill.SourceVar.IsEmpty())
	{
		FString OldColor = Node->Visual.Fill.SolidColor;
		Node->Visual.Fill.SolidColor = Resolver.ResolveColor(Node->Visual.Fill.SourceVar);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("ReThemeNode '%s': Fill %s -> %s (var=%s)"),
			*Node->Id, *OldColor, *Node->Visual.Fill.SolidColor, *Node->Visual.Fill.SourceVar);
	}

	for (auto& Stop : Node->Visual.Fill.Gradient.Colors)
	{
		if (!Stop.SourceVar.IsEmpty())
			Stop.Color = Resolver.ResolveColor(Stop.SourceVar);
	}

	// Multi-layer fills
	for (FDesignFill& F : Node->Visual.Fills)
	{
		if (!F.SourceVar.IsEmpty())
			F.SolidColor = Resolver.ResolveColor(F.SourceVar);
		for (auto& Stop : F.Gradient.Colors)
		{
			if (!Stop.SourceVar.IsEmpty())
				Stop.Color = Resolver.ResolveColor(Stop.SourceVar);
		}
	}

	if (Node->Visual.Stroke.Fill.IsSet() && !Node->Visual.Stroke.Fill->SourceVar.IsEmpty())
		Node->Visual.Stroke.Fill->SolidColor = Resolver.ResolveColor(Node->Visual.Stroke.Fill->SourceVar);

	for (auto& Effect : Node->Visual.Effects)
	{
		if (!Effect.Shadow.ColorSourceVar.IsEmpty())
			Effect.Shadow.Color = Resolver.ResolveColor(Effect.Shadow.ColorSourceVar);
	}

	for (auto& Child : Node->Children)
		ReThemeNode(Child, Resolver);
}



