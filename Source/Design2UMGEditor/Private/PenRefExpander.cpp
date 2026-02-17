// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenRefExpander.h"
#include "Design2UMGLog.h"

namespace
{
	void ApplyOverrideProperties(FPenNode& Target, const FPenNode& Override)
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
		if (!Override.Width.IsFit())
			Target.Width = Override.Width;
		if (!Override.Height.IsFit())
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
			Target.Padding = Override.Padding;
		if (Override.bHasGap)
			Target.Gap = Override.Gap;
		if (Override.bHasClip)
			Target.bClip = Override.bClip;
		if (Override.bHasOpacity)
			Target.Opacity = Override.Opacity;
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

	TSharedPtr<FPenNode> MergeDescendantOverrideNodes(
		const TSharedPtr<FPenNode>& BaseOverride,
		const TSharedPtr<FPenNode>& OuterOverride)
	{
		if (!OuterOverride.IsValid())
			return BaseOverride;
		if (!BaseOverride.IsValid())
			return FPenRefExpander::DeepCopyNode(OuterOverride);
		if (OuterOverride->Type != EPenNodeType::Unknown)
			return FPenRefExpander::DeepCopyNode(OuterOverride);

		auto Merged = FPenRefExpander::DeepCopyNode(BaseOverride);
		if (!Merged.IsValid())
			return FPenRefExpander::DeepCopyNode(OuterOverride);

		ApplyOverrideProperties(*Merged, *OuterOverride);
		return Merged;
	}

	void MergeDescendants(
		TMap<FString, TSharedPtr<FPenNode>>& InOut,
		const TMap<FString, TSharedPtr<FPenNode>>& Outer)
	{
		for (const auto& Pair : Outer)
		{
			TSharedPtr<FPenNode> Existing = InOut.FindRef(Pair.Key);
			InOut.Add(Pair.Key, MergeDescendantOverrideNodes(Existing, Pair.Value));
		}
	}

	void ApplyInstanceOverrides(TSharedPtr<FPenNode>& Expanded, const TSharedPtr<FPenNode>& RefNode)
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
			Expanded->Padding = RefNode->Padding;
		if (RefNode->bHasClip)
			Expanded->bClip = RefNode->bClip;
		if (!RefNode->bEnabled)
			Expanded->bEnabled = false;
		if (RefNode->bHasOpacity)
			Expanded->Opacity = RefNode->Opacity;
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

	TSharedPtr<FPenNode> ExpandRefInternal(
		const TSharedPtr<FPenNode>& RefNode,
		const FPenDocument& Doc,
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

		auto Expanded = FPenRefExpander::DeepCopyNode(SourceNode);
		if (!Expanded.IsValid())
		{
			RefChain.Remove(RefTargetId);
			return nullptr;
		}
		MergeDescendants(Expanded->Descendants, RefNode->Descendants);
		ApplyInstanceOverrides(Expanded, RefNode);

		TSharedPtr<FPenNode> Result = Expanded;
		if (Expanded->Type == EPenNodeType::Ref && !Expanded->Ref.IsEmpty())
		{
			Result = ExpandRefInternal(Expanded, Doc, RefChain);
		}
		else
		{
			for (const auto& Pair : Expanded->Descendants)
				FPenRefExpander::ApplyDescendantOverride(Result, Pair.Key, Pair.Value);
		}

		RefChain.Remove(RefTargetId);
		return Result;
	}
}

TSharedPtr<FPenNode> FPenRefExpander::DeepCopyNode(const TSharedPtr<FPenNode>& Source)
{
	if (!Source.IsValid())
		return nullptr;

	auto Copy = MakeShared<FPenNode>();
	*Copy = *Source;

	Copy->Children.Empty();
	for (const auto& Child : Source->Children)
		Copy->Children.Add(DeepCopyNode(Child));

	return Copy;
}

TSharedPtr<FPenNode> FPenRefExpander::ExpandRef(const TSharedPtr<FPenNode>& RefNode, const FPenDocument& Doc)
{
	TSet<FString> RefChain;
	return ExpandRefInternal(RefNode, Doc, RefChain);
}

void FPenRefExpander::ApplyDescendantOverride(TSharedPtr<FPenNode>& Node, const FString& Path, const TSharedPtr<FPenNode>& Override)
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

			// 对象替换模式
			if (Override->Type != EPenNodeType::Unknown)
			{
				Node->Children[i] = Override;
				return;
			}

			ApplyOverrideProperties(*Child, *Override);
			return;
		}
	}
}

void FPenRefExpander::ReThemeNode(TSharedPtr<FPenNode>& Node, const FPenVariableResolver& Resolver)
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
