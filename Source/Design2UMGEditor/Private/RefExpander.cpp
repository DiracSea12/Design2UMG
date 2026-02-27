// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "RefExpander.h"
#include "Design2UMGLog.h"

namespace
{
	void ApplyOverrideProperties(FDesignNode& Target, const FDesignNode& Override)
	{
		if (!Override.Content.IsEmpty())
			Target.Content = Override.Content;
		if (Override.FontSize > 0)
			Target.FontSize = Override.FontSize;
		if (!Override.FontWeight.IsEmpty())
			Target.FontWeight = Override.FontWeight;
		if (!Override.FontFamily.IsEmpty())
			Target.FontFamily = Override.FontFamily;
		if (Override.Fill.Type != EPenFillType::None || Override.Fill.bExplicitClear)
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
		if (Override.JustifyContent != EPenJustifyContent::Start)
			Target.JustifyContent = Override.JustifyContent;
		if (Override.AlignItems != EPenAlignItems::Start)
			Target.AlignItems = Override.AlignItems;
		if (Override.TextAlign != EPenTextAlign::Left)
			Target.TextAlign = Override.TextAlign;
		if (Override.LineHeight > 0)
			Target.LineHeight = Override.LineHeight;
		if (Override.TextGrowth != EPenTextGrowth::Auto)
			Target.TextGrowth = Override.TextGrowth;
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
		if (OuterOverride->Type != EPenNodeType::Unknown)
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
		if (RefNode->bHasY)
		{
			Expanded->Y = RefNode->Y;
			Expanded->bHasY = true;
		}
		if (RefNode->bHasWidth)
		{
			Expanded->Width = RefNode->Width;
			Expanded->bHasWidth = true;
		}
		if (RefNode->bHasHeight)
		{
			Expanded->Height = RefNode->Height;
			Expanded->bHasHeight = true;
		}

		if (RefNode->Fill.Type != EPenFillType::None || RefNode->Fill.bExplicitClear)
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
		if (Expanded->Type == EPenNodeType::Ref && !Expanded->Ref.IsEmpty())
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

	// 深拷贝 Children
	Copy->Children.Empty();
	for (const auto& Child : Source->Children)
		Copy->Children.Add(DeepCopyNode(Child));

	// 深拷贝 Descendants，避免多实例共享节点导致交叉污染
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
		for (int32 i = 0; i < Node->Children.Num(); ++i)
		{
			auto& Child = Node->Children[i];
			if (!Child.IsValid() || Child->Id != Path)
				continue;

			// 对象替换模式：深拷贝避免多实例共享同一节点
			if (Override->Type != EPenNodeType::Unknown)
			{
				Node->Children[i] = DeepCopyNode(Override);
				return;
			}

			ApplyOverrideProperties(*Child, *Override);
			return;
		}
	}
}

void FRefExpander::ReThemeNode(TSharedPtr<FDesignNode>& Node, const FPenVariableResolver& Resolver)
{
	if (!Node.IsValid())
		return;

	// 主填充
	if (!Node->Fill.SourceVar.IsEmpty())
	{
		FString OldColor = Node->Fill.SolidColor;
		Node->Fill.SolidColor = Resolver.ResolveColor(Node->Fill.SourceVar);
		UE_LOG(LogDesign2UMG, Log, TEXT("ReThemeNode '%s': Fill %s -> %s (var=%s)"),
			*Node->Id, *OldColor, *Node->Fill.SolidColor, *Node->Fill.SourceVar);
	}

	for (auto& Stop : Node->Fill.Gradient.Colors)
	{
		if (!Stop.SourceVar.IsEmpty())
			Stop.Color = Resolver.ResolveColor(Stop.SourceVar);
	}

	// 多层填充
	for (FPenFill& F : Node->Fills)
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
