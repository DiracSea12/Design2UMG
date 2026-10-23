// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "RefExpander.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignTreeLimits.h"
#include "Design2UMGLog.h"
#include "PenParserInternal.h"
#include "PenParserNodeExtensions.h"

namespace
{
	const TCHAR* ExpandedRefInstanceMetadataKey = TEXT("design2umg.expanded_ref_instance");

	template <typename T, typename Destination>
	void AssignIfSet(const TOptional<T>& Value, Destination& Target)
	{
		if (Value.IsSet()) Target = Value.GetValue();
	}

	TArray<TSharedPtr<FDesignNode>> CopyChildren(const TArray<TSharedPtr<FDesignNode>>& Children)
	{
		TArray<TSharedPtr<FDesignNode>> Copy;
		Copy.Reserve(Children.Num());
		for (const TSharedPtr<FDesignNode>& Child : Children)
		{
			Copy.Add(RefExpander::DeepCopyNode(Child));
		}
		return Copy;
	}

	FDesignNodeOverride CopyOverride(const FDesignNodeOverride& Override);
	void MergeOverride(FDesignNodeOverride& Target, const FDesignNodeOverride& Outer);
	void MergeDescendants(TMap<FString, FDesignNodeOverride>& Target, const TMap<FString, FDesignNodeOverride>& Outer);

	FDesignNodePatch CopyPatch(const FDesignNodePatch& Patch)
	{
		FDesignNodePatch Copy = Patch;
		for (auto& Pair : Copy.Descendants) Pair.Value = MakeShared<FDesignNodeOverride>(CopyOverride(*Pair.Value));
		if (Patch.Children.IsSet()) Copy.Children = CopyChildren(Patch.Children.GetValue());
		return Copy;
	}

	TSharedPtr<FJsonObject> MergePencilProperties(const TSharedPtr<FJsonObject>& Base, const TSharedPtr<FJsonObject>& Override)
	{
		if (!Override.IsValid()) return Base;
		auto Merged = MakeShared<FJsonObject>();
		if (Base.IsValid()) Merged->Values = Base->Values;
		Merged->Values.Append(Override->Values);
		// Script instance input overrides replace individual parameters, not the whole input object.
		const auto BaseInputs = Base.IsValid() ? Base->TryGetField(TEXT("inputs")) : nullptr;
		const auto OverrideInputs = Override->TryGetField(TEXT("inputs"));
		if (BaseInputs.IsValid() && OverrideInputs.IsValid()
			&& BaseInputs->Type == EJson::Object && OverrideInputs->Type == EJson::Object)
		{
			auto Inputs = MakeShared<FJsonObject>();
			Inputs->Values = BaseInputs->AsObject()->Values;
			Inputs->Values.Append(OverrideInputs->AsObject()->Values);
			Merged->SetObjectField(TEXT("inputs"), Inputs);
		}
		return Merged;
	}

	bool ContainsVariable(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid()) return false;
		if (Value->Type == EJson::String) return FDesignVariableResolver::IsVariableRef(Value->AsString());
		if (Value->Type == EJson::Array)
			for (const auto& Item : Value->AsArray()) { if (ContainsVariable(Item)) return true; }
		if (Value->Type == EJson::Object)
			for (const auto& Pair : Value->AsObject()->Values) { if (ContainsVariable(Pair.Value)) return true; }
		return false;
	}

	void MergePatch(FDesignNodePatch& Target, const FDesignNodePatch& Outer)
	{
		for (const auto& Pair : Outer.Descendants)
		{
			FDesignNodeOverride Combined;
			if (const auto* Previous = Target.Descendants.Find(Pair.Key)) Combined = CopyOverride(**Previous);
			MergeOverride(Combined, *Pair.Value);
			Target.Descendants.Add(Pair.Key, MakeShared<FDesignNodeOverride>(MoveTemp(Combined)));
		}
		AssignIfSet(Outer.Ref, Target.Ref);
		AssignIfSet(Outer.Reusable, Target.Reusable);
		AssignIfSet(Outer.AbsoluteLayoutPositioning, Target.AbsoluteLayoutPositioning);
		AssignIfSet(Outer.SourceContext, Target.SourceContext);
		AssignIfSet(Outer.SourceModel, Target.SourceModel);
		AssignIfSet(Outer.SourceMetadataJson, Target.SourceMetadataJson);
		AssignIfSet(Outer.SourceSlotRecommendations, Target.SourceSlotRecommendations);
		Target.PencilProperties = MergePencilProperties(Target.PencilProperties, Outer.PencilProperties);
		if (Outer.Theme.IsSet())
		{
			if (!Target.Theme.IsSet()) Target.Theme.Emplace();
			Target.Theme->Append(Outer.Theme.GetValue());
		}
		AssignIfSet(Outer.Name, Target.Name);
		AssignIfSet(Outer.X, Target.X);
		AssignIfSet(Outer.Y, Target.Y);
		AssignIfSet(Outer.Width, Target.Width);
		AssignIfSet(Outer.Height, Target.Height);
		AssignIfSet(Outer.Rotation, Target.Rotation);
		AssignIfSet(Outer.Enabled, Target.Enabled);
		AssignIfSet(Outer.Opacity, Target.Opacity);
		AssignIfSet(Outer.FlipX, Target.FlipX);
		AssignIfSet(Outer.FlipY, Target.FlipY);
		AssignIfSet(Outer.Layout, Target.Layout);
		AssignIfSet(Outer.Gap, Target.Gap);
		AssignIfSet(Outer.Padding, Target.Padding);
		AssignIfSet(Outer.PaddingRequiresRuntimeWrap, Target.PaddingRequiresRuntimeWrap);
		AssignIfSet(Outer.JustifyContent, Target.JustifyContent);
		AssignIfSet(Outer.AlignItems, Target.AlignItems);
		AssignIfSet(Outer.Clip, Target.Clip);
		AssignIfSet(Outer.LayoutGrow, Target.LayoutGrow);
		AssignIfSet(Outer.SemanticPlaceholderSurface, Target.SemanticPlaceholderSurface);
		AssignIfSet(Outer.Fills, Target.Fills);
		AssignIfSet(Outer.StrokeFill, Target.StrokeFill);
		AssignIfSet(Outer.StrokeThickness, Target.StrokeThickness);
		AssignIfSet(Outer.StrokeAlign, Target.StrokeAlign);
		AssignIfSet(Outer.StrokeCap, Target.StrokeCap);
		AssignIfSet(Outer.StrokeJoin, Target.StrokeJoin);
		AssignIfSet(Outer.CornerRadius, Target.CornerRadius);
		AssignIfSet(Outer.Effects, Target.Effects);
		if (Outer.Content.IsSet())
		{
			Target.Content = Outer.Content;
			Target.TextRuns = Outer.TextRuns;
			Target.Metadata.Remove(TEXT("text.content.raw"));
		}
		AssignIfSet(Outer.FontFamily, Target.FontFamily);
		AssignIfSet(Outer.FontPostScriptName, Target.FontPostScriptName);
		AssignIfSet(Outer.FontSize, Target.FontSize);
		AssignIfSet(Outer.FontWeight, Target.FontWeight);
		AssignIfSet(Outer.FontStyle, Target.FontStyle);
		AssignIfSet(Outer.LineHeight, Target.LineHeight);
		AssignIfSet(Outer.LetterSpacing, Target.LetterSpacing);
		AssignIfSet(Outer.TextAlign, Target.TextAlign);
		AssignIfSet(Outer.TextAlignVertical, Target.TextAlignVertical);
		AssignIfSet(Outer.TextGrowth, Target.TextGrowth);
		AssignIfSet(Outer.Underline, Target.Underline);
		AssignIfSet(Outer.Strikethrough, Target.Strikethrough);
		AssignIfSet(Outer.Href, Target.Href);
		AssignIfSet(Outer.IconFontName, Target.IconFontName);
		AssignIfSet(Outer.IconFontFamily, Target.IconFontFamily);
		AssignIfSet(Outer.IconWeight, Target.IconWeight);
		AssignIfSet(Outer.InnerRadius, Target.InnerRadius);
		AssignIfSet(Outer.StartAngle, Target.StartAngle);
		AssignIfSet(Outer.SweepAngle, Target.SweepAngle);
		AssignIfSet(Outer.PolygonCount, Target.PolygonCount);
		AssignIfSet(Outer.Geometry, Target.Geometry);
		AssignIfSet(Outer.ViewBox, Target.ViewBox);
		AssignIfSet(Outer.FillRule, Target.FillRule);
		AssignIfSet(Outer.Children, Target.Children);
		Target.Metadata.Append(Outer.Metadata);
	}

	void ApplyPatch(FDesignNode& Target, const FDesignNodePatch& Patch)
	{
		AssignIfSet(Patch.Ref, Target.Ref);
		AssignIfSet(Patch.Reusable, Target.bReusable);
		AssignIfSet(Patch.AbsoluteLayoutPositioning, Target.LayoutData.bAbsoluteLayoutPositioning);
		if (Patch.Theme.IsSet()) Target.Theme.Append(Patch.Theme.GetValue());
		if (Patch.PencilProperties.IsValid())
		{
			auto& Source = Target.MutableSource().Pencil;
			Source.Properties = MergePencilProperties(Source.Properties, Patch.PencilProperties);
			AssignIfSet(Patch.SourceContext, Source.Context);
			AssignIfSet(Patch.SourceModel, Source.Model);
			AssignIfSet(Patch.SourceMetadataJson, Source.MetadataJson);
			AssignIfSet(Patch.SourceSlotRecommendations, Source.SlotRecommendations);
		}
		AssignIfSet(Patch.Name, Target.Name);
		AssignIfSet(Patch.Rotation, Target.Transform.Rotation);
		AssignIfSet(Patch.Enabled, Target.Transform.bEnabled);
		AssignIfSet(Patch.FlipX, Target.Transform.bFlipX);
		AssignIfSet(Patch.FlipY, Target.Transform.bFlipY);
		AssignIfSet(Patch.Layout, Target.LayoutData.Layout);
		AssignIfSet(Patch.Gap, Target.LayoutData.ExplicitGap);
		AssignIfSet(Patch.Padding, Target.LayoutData.ExplicitPadding);
		AssignIfSet(Patch.PaddingRequiresRuntimeWrap, Target.LayoutData.PaddingRequiresRuntimeWrap);
		AssignIfSet(Patch.JustifyContent, Target.LayoutData.JustifyContent);
		AssignIfSet(Patch.AlignItems, Target.LayoutData.AlignItems);
		AssignIfSet(Patch.Clip, Target.LayoutData.ExplicitClip);
		AssignIfSet(Patch.LayoutGrow, Target.LayoutData.LayoutGrow);
		AssignIfSet(Patch.SemanticPlaceholderSurface, Target.LayoutData.bSemanticPlaceholderSurface);
		if (Patch.StrokeFill.IsSet())
		{
			if (Patch.StrokeFill->IsSet())
			{
				Target.Visual.Stroke.Fill = Patch.StrokeFill->GetValue();
			}
			else
			{
				Target.Visual.Stroke.Fill.Reset();
			}
		}
		AssignIfSet(Patch.StrokeThickness, Target.Visual.Stroke.Thickness);
		AssignIfSet(Patch.StrokeAlign, Target.Visual.Stroke.Align);
		AssignIfSet(Patch.StrokeCap, Target.Visual.Stroke.Cap);
		AssignIfSet(Patch.StrokeJoin, Target.Visual.Stroke.Join);
		AssignIfSet(Patch.CornerRadius, Target.Visual.CornerRadius);
		AssignIfSet(Patch.FontFamily, Target.Text.FontFamily);
		AssignIfSet(Patch.FontPostScriptName, Target.Text.FontPostScriptName);
		AssignIfSet(Patch.FontSize, Target.Text.FontSize);
		AssignIfSet(Patch.FontWeight, Target.Text.FontWeight);
		AssignIfSet(Patch.FontStyle, Target.Text.FontStyle);
		AssignIfSet(Patch.LineHeight, Target.Text.LineHeight);
		AssignIfSet(Patch.LetterSpacing, Target.Text.LetterSpacing);
		AssignIfSet(Patch.TextAlign, Target.Text.TextAlign);
		AssignIfSet(Patch.TextAlignVertical, Target.Text.TextAlignVertical);
		AssignIfSet(Patch.TextGrowth, Target.Text.TextGrowth);
		AssignIfSet(Patch.Underline, Target.Text.bUnderline);
		AssignIfSet(Patch.Strikethrough, Target.Text.bStrikethrough);
		AssignIfSet(Patch.Href, Target.Text.Href);
		AssignIfSet(Patch.IconFontName, Target.Text.IconFontName);
		AssignIfSet(Patch.IconFontFamily, Target.Text.IconFontFamily);
		AssignIfSet(Patch.IconWeight, Target.Text.IconWeight);
		AssignIfSet(Patch.InnerRadius, Target.Shape.InnerRadius);
		AssignIfSet(Patch.StartAngle, Target.Shape.StartAngle);
		AssignIfSet(Patch.SweepAngle, Target.Shape.SweepAngle);
		AssignIfSet(Patch.PolygonCount, Target.Shape.PolygonCount);
		AssignIfSet(Patch.Geometry, Target.Shape.Geometry);
		AssignIfSet(Patch.ViewBox, Target.Shape.ViewBox);
		AssignIfSet(Patch.FillRule, Target.Shape.FillRule);
		if (Patch.X.IsSet())
		{
			Target.Transform.X = Patch.X.GetValue();
			Target.Transform.ExplicitX = Patch.X;
		}
		if (Patch.Y.IsSet())
		{
			Target.Transform.Y = Patch.Y.GetValue();
			Target.Transform.ExplicitY = Patch.Y;
		}
		if (Patch.Width.IsSet()) Target.Transform.SetWidthValue(Patch.Width.GetValue());
		if (Patch.Height.IsSet()) Target.Transform.SetHeightValue(Patch.Height.GetValue());
		if (Patch.Opacity.IsSet()) Target.Transform.SetOpacityValue(Patch.Opacity.GetValue());
		if (Patch.Fills.IsSet())
		{
			Target.Visual.Fills = Patch.Fills.GetValue();
			Target.Visual.bFillExplicitClear = Target.Visual.Fills.IsEmpty();
		}
		if (Patch.Effects.IsSet())
		{
			Target.Visual.Effects = Patch.Effects.GetValue();
			Target.Visual.bEffectsExplicitClear = Target.Visual.Effects.IsEmpty();
		}
		if (Patch.Content.IsSet())
		{
			Target.Text.Content = Patch.Content.GetValue();
			Target.Text.TextRuns = Patch.TextRuns.Get(TArray<FDesignTextRun>());
			Target.Metadata.Remove(TEXT("text.content.raw"));
		}
		if (Patch.Children.IsSet())
		{
			Target.Children = CopyChildren(Patch.Children.GetValue());
			Target.Descendants.Reset();
		}
		TMap<FString, FDesignNodeOverride> Descendants;
		for (const auto& Pair : Patch.Descendants) Descendants.Add(Pair.Key, CopyOverride(*Pair.Value));
		MergeDescendants(Target.Descendants, Descendants);
		Target.Metadata.Append(Patch.Metadata);
		if (Patch.PencilProperties.IsValid()) PenParserNodeExtensions::RefreshPenSemanticMetadata(Target);

		// A patched descendant can itself be a ref. Preserve its authored patch for its later expansion.
		if (Target.Type == EDesignNodeType::Ref)
		{
			FDesignNodePatch Combined = Target.RefOverrides.IsValid() ? *Target.RefOverrides : FDesignNodePatch();
			MergePatch(Combined, Patch);
			Target.RefOverrides = MakeShared<FDesignNodePatch>(MoveTemp(Combined));
		}
	}

	FDesignNodeOverride CopyOverride(const FDesignNodeOverride& Override)
	{
		FDesignNodeOverride Copy;
		Copy.Replacement = RefExpander::DeepCopyNode(Override.Replacement);
		Copy.Patch = CopyPatch(Override.Patch);
		return Copy;
	}

	void MergeOverride(FDesignNodeOverride& Target, const FDesignNodeOverride& Outer)
	{
		if (Outer.Replacement.IsValid()) Target = CopyOverride(Outer);
		else MergePatch(Target.Patch, Outer.Patch);
	}

	bool ReplacesSubtree(const FDesignNodeOverride& Override)
	{
		return Override.Replacement.IsValid() || Override.Patch.Children.IsSet();
	}

	void DiscardReplacedDescendantPatches(
		TMap<FString, FDesignNodeOverride>& Descendants,
		const FString& Path,
		const FDesignNodeOverride& Override)
	{
		if (!ReplacesSubtree(Override)) return;
		const FString Prefix = Path + TEXT("/");
		for (auto It = Descendants.CreateIterator(); It; ++It)
		{
			if (It.Key().StartsWith(Prefix, ESearchCase::CaseSensitive)) It.RemoveCurrent();
		}
	}

	void MergeDescendants(
		TMap<FString, FDesignNodeOverride>& Target,
		const TMap<FString, FDesignNodeOverride>& Outer)
	{
		// Discard the old subtree's patches before adding any patches from the outer layer.
		TSet<FString> ReplacedPaths;
		for (const auto& Pair : Outer)
		{
			if (ReplacesSubtree(Pair.Value)) ReplacedPaths.Add(Pair.Key);
		}
		if (!ReplacedPaths.IsEmpty())
		{
			for (auto It = Target.CreateIterator(); It; ++It)
			{
				const FString& Path = It.Key();
				for (int32 Index = 0; Index < Path.Len(); ++Index)
				{
					if (Path[Index] != TCHAR('/')) continue;
					const FString Ancestor = Path.Left(Index);
					const FString* ReplacedPath = ReplacedPaths.Find(Ancestor);
					if (ReplacedPath && Ancestor.Equals(*ReplacedPath, ESearchCase::CaseSensitive))
					{
						It.RemoveCurrent();
						break;
					}
				}
			}
		}
		for (const auto& Pair : Outer)
		{
			MergeOverride(Target.FindOrAdd(Pair.Key), Pair.Value);
		}
	}

	void ApplyDescendants(TSharedPtr<FDesignNode>& Node, const TMap<FString, FDesignNodeOverride>& Descendants)
	{
		TArray<FString> Paths;
		Descendants.GetKeys(Paths);
		// Within one layer, establish parent structures before more specific child patches.
		Paths.Sort([&Descendants](const FString& A, const FString& B)
		{
			int32 ADepth = 0;
			int32 BDepth = 0;
			for (const TCHAR Ch : A) ADepth += Ch == TCHAR('/');
			for (const TCHAR Ch : B) BDepth += Ch == TCHAR('/');
			if (ADepth != BDepth) return ADepth < BDepth;
			const bool bAStructural = ReplacesSubtree(Descendants.FindChecked(A));
			const bool bBStructural = ReplacesSubtree(Descendants.FindChecked(B));
			return bAStructural != bBStructural ? bAStructural : A.Compare(B, ESearchCase::CaseSensitive) < 0;
		});
		for (const FString& Path : Paths)
		{
			RefExpander::ApplyDescendantOverride(Node, Path, Descendants.FindChecked(Path));
		}
	}

	bool ApplyOverrideToDirectChild(
		TSharedPtr<FDesignNode>& Parent,
		const FString& TargetId,
		const FDesignNodeOverride& Override)
	{
		if (!Parent.IsValid())
			return false;

		for (int32 i = 0; i < Parent->Children.Num(); ++i)
		{
			auto& Child = Parent->Children[i];
			if (!Child.IsValid() || Child->Id != TargetId)
				continue;

			if (Override.Replacement.IsValid())
				Child = RefExpander::DeepCopyNode(Override.Replacement);
			if (Child.IsValid()) ApplyPatch(*Child, Override.Patch);
			return true;
		}

		return false;
	}

	bool ApplyOverrideByIdRecursive(
		TSharedPtr<FDesignNode>& Node,
		const FString& TargetId,
		const FDesignNodeOverride& Override,
		int32 Depth = 0)
	{
		// Ref-override recursion cap: bounds override application inside one expansion, deliberately
		// independent of (and tighter than) DesignTreeLimits::MaxNodeDepth, which bounds whole-tree parse depth.
		constexpr int32 MaxDepth = 100;
		if (Depth > MaxDepth)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ApplyOverrideByIdRecursive: max depth %d exceeded for target '%s'"), MaxDepth, *TargetId);
			return false;
		}

		if (!Node.IsValid())
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

		Node->bExpandedRefInstance = true;
		Node->Metadata.Add(ExpandedRefInstanceMetadataKey, TEXT("true"));
		for (TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			MarkExpandedRefInstanceRecursive(Child);
		}
	}

	void ApplyInstanceOverrides(TSharedPtr<FDesignNode>& Expanded, const TSharedPtr<FDesignNode>& RefNode)
	{
		if (RefNode->RefOverrides.IsValid())
		{
			FDesignNodePatch InstanceProperties = *RefNode->RefOverrides;
			// The ref selects this definition; it is not an override of a definition's own ref target.
			InstanceProperties.Ref.Reset();
			InstanceProperties.Descendants.Reset();
			ApplyPatch(*Expanded, InstanceProperties);
		}
		Expanded->Id = RefNode->Id;

		// An instance without authored coordinates starts at its local origin, not the component canvas position.
		Expanded->Transform.X = RefNode->Transform.ExplicitX.Get(0.0f);
		Expanded->Transform.Y = RefNode->Transform.ExplicitY.Get(0.0f);
		Expanded->Transform.ExplicitX = RefNode->Transform.ExplicitX;
		Expanded->Transform.ExplicitY = RefNode->Transform.ExplicitY;

		// These flags also cover dimensions synthesized by SizeEstimator, not just authored Patch values.
		if (RefNode->Transform.bHasWidth)
		{
			Expanded->Transform.Width = RefNode->Transform.Width;
			Expanded->Transform.bHasWidth = true;
			Expanded->Transform.bSourceHasWidth = RefNode->Transform.bSourceHasWidth;
		}
		if (RefNode->Transform.bHasHeight)
		{
			Expanded->Transform.Height = RefNode->Transform.Height;
			Expanded->Transform.bHasHeight = true;
			Expanded->Transform.bSourceHasHeight = RefNode->Transform.bSourceHasHeight;
		}
	}

	FDesignReferenceExpansionResult MakeExpansionFailure(
		const EDesignReferenceExpansionStatus Status,
		const FString& NodeId,
		const FString& ReferencedNodeId,
		FString Message)
	{
		FDesignReferenceExpansionResult Result;
		Result.Status = Status;
		Result.NodeId = NodeId;
		Result.ReferencedNodeId = ReferencedNodeId;
		Result.Message = MoveTemp(Message);
		return Result;
	}

	FDesignReferenceExpansionResult ExpandRefInternal(
		const TSharedPtr<FDesignNode>& RefNode,
		const TMap<FString, TSharedPtr<FDesignNode>>& ReferenceTargetsById,
		TSet<FString>& RefChain,
		const FString& EntryNodeId)
	{
		if (!RefNode.IsValid() || RefNode->Ref.IsEmpty())
		{
			return MakeExpansionFailure(
				EDesignReferenceExpansionStatus::InvalidReference,
				EntryNodeId,
				FString(),
				FString::Printf(TEXT("Reference expansion skipped node '%s': a valid target id is required."), *EntryNodeId));
		}

		const FString RefTargetId = RefNode->Ref;
		if (RefChain.Contains(RefTargetId))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("ExpandRef: circular ref detected at '%s' -> '%s'"),
				*RefNode->Id, *RefTargetId);
			return MakeExpansionFailure(
				EDesignReferenceExpansionStatus::CircularReference,
				EntryNodeId,
				RefTargetId,
				FString::Printf(
					TEXT("Reference expansion failed for node '%s': circular reference reaches '%s'."),
					*EntryNodeId,
					*RefTargetId));
		}

		if (RefChain.Num() >= DesignTreeLimits::MaxReferenceChainDepth)
		{
			UE_LOG(
				LogDesign2UMG,
				Error,
				TEXT("ExpandRef: reference chain limit %d exceeded at '%s' -> '%s'"),
				DesignTreeLimits::MaxReferenceChainDepth,
				*RefNode->Id,
				*RefTargetId);
			return MakeExpansionFailure(
				EDesignReferenceExpansionStatus::ChainLimitExceeded,
				EntryNodeId,
				RefTargetId,
				FString::Printf(
					TEXT("Reference expansion failed for node '%s': reference chain exceeds %d targets at '%s'."),
					*EntryNodeId,
					DesignTreeLimits::MaxReferenceChainDepth,
					*RefTargetId));
		}

		RefChain.Add(RefTargetId);

		const TSharedPtr<FDesignNode>* SourceNode = ReferenceTargetsById.Find(RefTargetId);
		if (!SourceNode)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("ExpandRef: ref '%s' -> '%s' NOT FOUND"), *RefNode->Id, *RefTargetId);
			RefChain.Remove(RefTargetId);
			return MakeExpansionFailure(
				EDesignReferenceExpansionStatus::MissingTarget,
				EntryNodeId,
				RefTargetId,
				FString::Printf(
					TEXT("Reference expansion skipped node '%s': target '%s' was not found."),
					*EntryNodeId,
					*RefTargetId));
		}

		auto Expanded = RefExpander::DeepCopyNode(*SourceNode);
		if (!Expanded.IsValid())
		{
			RefChain.Remove(RefTargetId);
			return MakeExpansionFailure(
				EDesignReferenceExpansionStatus::InvalidReference,
				EntryNodeId,
				RefTargetId,
				FString::Printf(
					TEXT("Reference expansion skipped node '%s': target '%s' could not be copied."),
					*EntryNodeId,
					*RefTargetId));
		}
		ApplyInstanceOverrides(Expanded, RefNode);
		MergeDescendants(Expanded->Descendants, RefNode->Descendants);

		FDesignReferenceExpansionResult Result;
		if (Expanded->Type == EDesignNodeType::Ref && !Expanded->Ref.IsEmpty())
		{
			Result = ExpandRefInternal(Expanded, ReferenceTargetsById, RefChain, EntryNodeId);
		}
		else
		{
			ApplyDescendants(Expanded, Expanded->Descendants);
			Result.ExpandedNode = MoveTemp(Expanded);
			Result.Status = EDesignReferenceExpansionStatus::Succeeded;
			Result.NodeId = EntryNodeId;
		}

		if (Result.IsSuccess() && !RefNode->bReusable)
		{
			MarkExpandedRefInstanceRecursive(Result.ExpandedNode);
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

	Copy->Children = CopyChildren(Source->Children);
	if (Source->RefOverrides.IsValid())
		Copy->RefOverrides = MakeShared<FDesignNodePatch>(CopyPatch(*Source->RefOverrides));
	for (auto& Pair : Copy->Descendants)
	{
		Pair.Value = CopyOverride(Pair.Value);
	}

	return Copy;
}

FDesignReferenceExpansionResult RefExpander::ExpandRef(
	const TSharedPtr<FDesignNode>& RefNode,
	const FDesignDocument& Doc)
{
	TMap<FString, TSharedPtr<FDesignNode>> ReferenceTargetsById;
	Doc.BuildReferenceNodeIndex(ReferenceTargetsById);
	return ExpandRef(RefNode, ReferenceTargetsById);
}

FDesignReferenceExpansionResult RefExpander::ExpandRef(
	const TSharedPtr<FDesignNode>& RefNode,
	const TMap<FString, TSharedPtr<FDesignNode>>& ReferenceTargetsById)
{
	TSet<FString> RefChain;
	const FString EntryNodeId = RefNode.IsValid() ? RefNode->Id : FString();
	return ExpandRefInternal(RefNode, ReferenceTargetsById, RefChain, EntryNodeId);
}

void RefExpander::ApplyDescendantOverride(TSharedPtr<FDesignNode>& Node, const FString& Path, const FDesignNodeOverride& Override)
{
	if (!Node.IsValid())
		return;

	// Defer a path through an unexpanded ref; its reusable children are not in this tree yet.
	if (Node->Type == EDesignNodeType::Ref)
	{
		DiscardReplacedDescendantPatches(Node->Descendants, Path, Override);
		MergeOverride(Node->Descendants.FindOrAdd(Path), Override);
		return;
	}

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

	FDesignVariableResolver ActiveResolver = Resolver;
	ActiveResolver.PushThemeOverride(Node->Theme);
	if (Node->Source().Pencil.Properties.IsValid())
	{
		auto VariableProperties = MakeShared<FJsonObject>();
		for (const auto& Pair : Node->Source().Pencil.Properties->Values)
			if (Pair.Key != TEXT("theme") && ContainsVariable(Pair.Value)) VariableProperties->SetField(Pair.Key, Pair.Value);
		if (!VariableProperties->Values.IsEmpty())
		{
			TSharedPtr<FDesignNode> Parsed = MakeShared<FDesignNode>(*Node);
			if (VariableProperties->HasField(TEXT("effect")) || VariableProperties->HasField(TEXT("effects"))) Parsed->Visual.Effects.Reset();
			if (VariableProperties->HasField(TEXT("content"))) Parsed->Text.TextRuns.Reset();
			PenParser::ParseNodeProperties(VariableProperties, Parsed, ActiveResolver);
			PenParserNodeExtensions::ParsePenSemanticMetadata(VariableProperties, *Parsed, ActiveResolver);
			ApplyPatch(*Node, PenParser::CaptureNodePatch(VariableProperties, *Parsed, ActiveResolver));
		}
	}

	for (FDesignFill& F : Node->Visual.Fills)
	{
		if (!F.SourceVar.IsEmpty())
			F.SolidColor = ActiveResolver.ResolveColor(F.SourceVar);
		for (auto& Stop : F.Gradient.Colors)
		{
			if (!Stop.SourceVar.IsEmpty())
				Stop.Color = ActiveResolver.ResolveColor(Stop.SourceVar);
		}
	}

	if (Node->Visual.Stroke.Fill.IsSet() && !Node->Visual.Stroke.Fill->SourceVar.IsEmpty())
		Node->Visual.Stroke.Fill->SolidColor = ActiveResolver.ResolveColor(Node->Visual.Stroke.Fill->SourceVar);

	for (auto& Effect : Node->Visual.Effects)
	{
		if (!Effect.Shadow.ColorSourceVar.IsEmpty())
			Effect.Shadow.Color = ActiveResolver.ResolveColor(Effect.Shadow.ColorSourceVar);
	}

	for (auto& Child : Node->Children)
		ReThemeNode(Child, ActiveResolver);
}
