// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenRefExpander.h"
#include "PenToUMGLog.h"

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
	if (!RefNode.IsValid() || RefNode->Ref.IsEmpty())
		return nullptr;

	auto SourceNode = Doc.FindReusableNode(RefNode->Ref);
	if (!SourceNode.IsValid())
	{
		UE_LOG(LogPenToUMG, Warning, TEXT("ExpandRef: ref '%s' -> '%s' NOT FOUND"), *RefNode->Id, *RefNode->Ref);
		return nullptr;
	}
	auto Expanded = DeepCopyNode(SourceNode);

	for (const auto& Pair : RefNode->Descendants)
		ApplyDescendantOverride(Expanded, Pair.Key, Pair.Value);

	Expanded->Id = RefNode->Id;
	if (!RefNode->Name.IsEmpty())
		Expanded->Name = RefNode->Name;

	if (RefNode->bHasX) Expanded->X = RefNode->X;
	if (RefNode->bHasY) Expanded->Y = RefNode->Y;
	if (RefNode->bHasWidth) Expanded->Width = RefNode->Width;
	if (RefNode->bHasHeight) Expanded->Height = RefNode->Height;

	if (RefNode->Fill.Type != EPenFillType::None || RefNode->Fill.bExplicitClear)
		Expanded->Fill = RefNode->Fill;
	if (RefNode->Stroke.HasStroke())
		Expanded->Stroke = RefNode->Stroke;
	if (!RefNode->Padding.IsZero())
		Expanded->Padding = RefNode->Padding;
	if (RefNode->bClip)
		Expanded->bClip = RefNode->bClip;
	if (!RefNode->bEnabled)
		Expanded->bEnabled = false;
	if (RefNode->Opacity < 1.0f)
		Expanded->Opacity = RefNode->Opacity;
	if (!FMath::IsNearlyZero(RefNode->Rotation))
		Expanded->Rotation = RefNode->Rotation;
	if (RefNode->bFlipX)
		Expanded->bFlipX = RefNode->bFlipX;
	if (RefNode->bFlipY)
		Expanded->bFlipY = RefNode->bFlipY;

	if (RefNode->Children.Num() > 0)
		Expanded->Children = RefNode->Children;

	return Expanded;
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

			// 属性覆盖模式
			if (!Override->Content.IsEmpty())
				Child->Content = Override->Content;
			if (Override->FontSize > 0)
				Child->FontSize = Override->FontSize;
			if (!Override->FontWeight.IsEmpty())
				Child->FontWeight = Override->FontWeight;
			if (!Override->FontFamily.IsEmpty())
				Child->FontFamily = Override->FontFamily;
			if (Override->Fill.Type != EPenFillType::None || Override->Fill.bExplicitClear)
				Child->Fill = Override->Fill;
			if (Override->Stroke.HasStroke())
				Child->Stroke = Override->Stroke;
			if (!Override->Width.IsFit())
				Child->Width = Override->Width;
			if (!Override->Height.IsFit())
				Child->Height = Override->Height;
			if (Override->bHasX)
				Child->X = Override->X;
			if (Override->bHasY)
				Child->Y = Override->Y;
			if (!Override->bEnabled)
				Child->bEnabled = Override->bEnabled;
			if (!Override->Padding.IsZero())
				Child->Padding = Override->Padding;
			if (Override->Gap > 0)
				Child->Gap = Override->Gap;
			if (Override->JustifyContent != EPenJustifyContent::Start)
				Child->JustifyContent = Override->JustifyContent;
			if (Override->AlignItems != EPenAlignItems::Start)
				Child->AlignItems = Override->AlignItems;
			if (Override->TextAlign != EPenTextAlign::Left)
				Child->TextAlign = Override->TextAlign;
			if (Override->LineHeight > 0)
				Child->LineHeight = Override->LineHeight;
			if (Override->TextGrowth != EPenTextGrowth::Auto)
				Child->TextGrowth = Override->TextGrowth;

			if (Override->Children.Num() > 0)
				Child->Children = Override->Children;

			return;
		}
	}
}

void FPenRefExpander::ReThemeNode(TSharedPtr<FPenNode>& Node, const FPenVariableResolver& Resolver)
{
	if (!Node.IsValid())
		return;

	if (!Node->Fill.SourceVar.IsEmpty())
	{
		FString OldColor = Node->Fill.SolidColor;
		Node->Fill.SolidColor = Resolver.ResolveColor(Node->Fill.SourceVar);
		UE_LOG(LogPenToUMG, Log, TEXT("ReThemeNode '%s': Fill %s -> %s (var=%s)"),
			*Node->Id, *OldColor, *Node->Fill.SolidColor, *Node->Fill.SourceVar);
	}

	for (auto& Stop : Node->Fill.Gradient.Colors)
	{
		if (!Stop.SourceVar.IsEmpty())
			Stop.Color = Resolver.ResolveColor(Stop.SourceVar);
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