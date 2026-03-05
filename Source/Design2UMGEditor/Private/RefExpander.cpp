// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "RefExpander.h"
#include "Design2UMGLog.h"

namespace
{
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
		if (!Override.Content.IsEmpty())
			Target.Content = Override.Content;
		if (Override.FontSize > 0)
			Target.FontSize = Override.FontSize;
		if (!Override.FontWeight.IsEmpty())
			Target.FontWeight = Override.FontWeight;
		if (!Override.FontStyle.IsEmpty())
			Target.FontStyle = Override.FontStyle;
		if (!Override.FontFamily.IsEmpty())
			Target.FontFamily = Override.FontFamily;
		if (Override.Fill.Type != EDesignFillType::None || Override.Fill.bExplicitClear)
		{
			Target.Fill = Override.Fill;
			Target.Fills = Override.Fills;
		}
		if (Override.Stroke.HasStroke())
			Target.Stroke = Override.Stroke;
		if (Override.bHasWidth)
			Target.Width = Override.Width;
		if (Override.bHasHeight)
			Target.Height = Override.Height;
		if (Override.bHasX)
		{
			Target.X = Override.X;
			Target.bHasX = true;
		}
		if (Override.bHasY)
		{
			Target.Y = Override.Y;
			Target.bHasY = true;
		}
		if (!Override.bEnabled)
			Target.bEnabled = false;
		if (Override.bHasPadding)
		{
			Target.Padding = Override.Padding;
			Target.bHasPadding = true;
		}
		if (Override.bHasGap)
		{
			Target.Gap = Override.Gap;
			Target.bHasGap = true;
		}
		if (Override.bHasClip)
		{
			Target.bClip = Override.bClip;
			Target.bHasClip = true;
		}
		if (Override.bHasOpacity)
		{
			Target.Opacity = Override.Opacity;
			Target.bHasOpacity = true;
		}
		if (Override.JustifyContent != EDesignJustifyContent::Start)
			Target.JustifyContent = Override.JustifyContent;
		if (Override.AlignItems != EDesignAlignItems::Start)
			Target.AlignItems = Override.AlignItems;
		if (Override.TextAlign != EDesignTextAlign::Left)
			Target.TextAlign = Override.TextAlign;
		if (Override.LineHeight > 0)
			Target.LineHeight = Override.LineHeight;
		if (Override.TextGrowth != EDesignTextGrowth::Auto)
			Target.TextGrowth = Override.TextGrowth;
		if (Override.bUnderline)
			Target.bUnderline = true;
		if (Override.bStrikethrough)
			Target.bStrikethrough = true;
		if (!Override.Href.IsEmpty())
			Target.Href = Override.Href;
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
			return FRefExpander::DeepCopyNode(OuterOverride);
		if (OuterOverride->Type != EDesignNodeType::Unknown)
			return FRefExpander::DeepCopyNode(OuterOverride);

		auto Merged = FRefExpander::DeepCopyNode(BaseOverride);
		if (!Merged.IsValid())
			return FRefExpander::DeepCopyNode(OuterOverride);

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
				Parent->Children[i] = FRefExpander::DeepCopyNode(Override);
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

	void ApplyInstanceOverrides(TSharedPtr<FDesignNode>& Expanded, const TSharedPtr<FDesignNode>& RefNode)
	{
		if (!Expanded.IsValid() || !RefNode.IsValid())
			return;

		Expanded->Id = RefNode->Id;
		if (!RefNode->Name.IsEmpty())
			Expanded->Name = RefNode->Name;

		if (RefNode->bHasX)
		{
			Expanded->X = RefNode->X;
			Expanded->bHasX = true;
		}
		else
		{
			// Instance with no explicit x should use local origin, not component definition canvas coordinate.
			Expanded->X = 0.0f;
			Expanded->bHasX = false;
		}
		if (RefNode->bHasY)
		{
			Expanded->Y = RefNode->Y;
			Expanded->bHasY = true;
		}
		else
		{
			// Instance with no explicit y should use local origin, not component definition canvas coordinate.
			Expanded->Y = 0.0f;
			Expanded->bHasY = false;
		}
		if (ShouldApplySizeOverride(RefNode->Width, RefNode->bHasWidth))
		{
			Expanded->Width = RefNode->Width;
			Expanded->bHasWidth = true;
		}
		if (ShouldApplySizeOverride(RefNode->Height, RefNode->bHasHeight))
		{
			Expanded->Height = RefNode->Height;
			Expanded->bHasHeight = true;
		}

		if (RefNode->Fill.Type != EDesignFillType::None || RefNode->Fill.bExplicitClear)
		{
			Expanded->Fill = RefNode->Fill;
			Expanded->Fills = RefNode->Fills;
		}
		if (RefNode->Stroke.HasStroke())
			Expanded->Stroke = RefNode->Stroke;
		if (RefNode->bHasPadding)
		{
			Expanded->Padding = RefNode->Padding;
			Expanded->bHasPadding = true;
		}
		if (RefNode->bHasGap)
		{
			Expanded->Gap = RefNode->Gap;
			Expanded->bHasGap = true;
		}
		if (RefNode->bHasClip)
		{
			Expanded->bClip = RefNode->bClip;
			Expanded->bHasClip = true;
		}
		if (!RefNode->bEnabled)
			Expanded->bEnabled = false;
		if (RefNode->bHasOpacity)
		{
			Expanded->Opacity = RefNode->Opacity;
			Expanded->bHasOpacity = true;
		}
		if (!FMath::IsNearlyZero(RefNode->Rotation))
			Expanded->Rotation = RefNode->Rotation;
		if (RefNode->bFlipX)
			Expanded->bFlipX = RefNode->bFlipX;
		if (RefNode->bFlipY)
			Expanded->bFlipY = RefNode->bFlipY;

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

		auto Expanded = FRefExpander::DeepCopyNode(SourceNode);
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
				FRefExpander::ApplyDescendantOverride(Result, Pair.Key, Pair.Value);
		}

		RefChain.Remove(RefTargetId);
		return Result;
	}
}

TSharedPtr<FDesignNode> FRefExpander::DeepCopyNode(const TSharedPtr<FDesignNode>& Source)
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

TSharedPtr<FDesignNode> FRefExpander::ExpandRef(const TSharedPtr<FDesignNode>& RefNode, const FDesignDocument& Doc)
{
	TSet<FString> RefChain;
	return ExpandRefInternal(RefNode, Doc, RefChain);
}

void FRefExpander::ApplyDescendantOverride(TSharedPtr<FDesignNode>& Node, const FString& Path, const TSharedPtr<FDesignNode>& Override)
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

void FRefExpander::ReThemeNode(TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver)
{
	if (!Node.IsValid())
		return;

	// Primary fill
	if (!Node->Fill.SourceVar.IsEmpty())
	{
		FString OldColor = Node->Fill.SolidColor;
		Node->Fill.SolidColor = Resolver.ResolveColor(Node->Fill.SourceVar);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("ReThemeNode '%s': Fill %s -> %s (var=%s)"),
			*Node->Id, *OldColor, *Node->Fill.SolidColor, *Node->Fill.SourceVar);
	}

	for (auto& Stop : Node->Fill.Gradient.Colors)
	{
		if (!Stop.SourceVar.IsEmpty())
			Stop.Color = Resolver.ResolveColor(Stop.SourceVar);
	}

	// Multi-layer fills
	for (FDesignFill& F : Node->Fills)
	{
		if (!F.SourceVar.IsEmpty())
			F.SolidColor = Resolver.ResolveColor(F.SourceVar);
		for (auto& Stop : F.Gradient.Colors)
		{
			if (!Stop.SourceVar.IsEmpty())
				Stop.Color = Resolver.ResolveColor(Stop.SourceVar);
		}
	}

	if (!Node->Stroke.Fill.SourceVar.IsEmpty())
		Node->Stroke.Fill.SolidColor = Resolver.ResolveColor(Node->Stroke.Fill.SourceVar);

	for (auto& Effect : Node->Effects)
	{
		if (!Effect.Shadow.ColorSourceVar.IsEmpty())
			Effect.Shadow.Color = Resolver.ResolveColor(Effect.Shadow.ColorSourceVar);
	}

	for (auto& Child : Node->Children)
		ReThemeNode(Child, Resolver);
}



