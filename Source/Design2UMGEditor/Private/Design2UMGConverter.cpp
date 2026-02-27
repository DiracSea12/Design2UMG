// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "PenTypeMapper.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "FontResolver.h"
#include "LayoutMapper.h"
#include "VisualMapper.h"
#include "RefExpander.h"

namespace
{
	constexpr int32 MaxTreeRecursionDepth = 512;

	thread_local int32 GConvertNodeDepth = 0;
	thread_local int32 GPrecomputeDepth = 0;
	thread_local int32 GEstimateDepth = 0;

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
				UE_LOG(LogDesign2UMG, Error, TEXT("设计文件节点嵌套层级过深（超过 %d 层），可能存在循环引用。"), MaxTreeRecursionDepth);
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

	const TCHAR* ToSizeModeLogString(EPenSizeMode Mode)
	{
		switch (Mode)
		{
		case EPenSizeMode::Fixed: return TEXT("fixed");
		case EPenSizeMode::FillContainer: return TEXT("fill");
		case EPenSizeMode::FitContent: return TEXT("fit");
		default: return TEXT("unknown");
		}
	}

	bool IsDesignerOnlyNodeType(EPenNodeType Type)
	{
		return Type == EPenNodeType::Connection
			|| Type == EPenNodeType::Note
			|| Type == EPenNodeType::Prompt
			|| Type == EPenNodeType::Context;
	}

	bool HasVisibleFill(const FPenFill& Fill)
	{
		return Fill.bEnabled && Fill.Type != EPenFillType::None;
	}

	bool HasRenderableVisual(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
			return false;

		if (Node->Type == EPenNodeType::Text || Node->Type == EPenNodeType::IconFont)
			return true;

		if (HasVisibleFill(Node->Fill))
			return true;

		for (const FPenFill& Fill : Node->Fills)
		{
			if (HasVisibleFill(Fill))
				return true;
		}

		if (Node->Stroke.HasStroke())
			return true;

		for (const FPenEffect& Effect : Node->Effects)
		{
			if (Effect.bEnabled)
				return true;
		}

		return false;
	}

	struct FChildSizeSnapshot
	{
		TSharedPtr<FDesignNode> Child;
		FPenSize RawWidth;
		FPenSize RawHeight;
		FPenSize EffectiveWidth;
		FPenSize EffectiveHeight;
		bool bUsedExpandedRef = false;
	};

	FChildSizeSnapshot BuildChildSizeSnapshot(const TSharedPtr<FDesignNode>& Child, const FDesignDocument& Doc)
	{
		FChildSizeSnapshot Snapshot;
		Snapshot.Child = Child;
		if (!Child.IsValid())
			return Snapshot;

		Snapshot.RawWidth = Child->Width;
		Snapshot.RawHeight = Child->Height;
		Snapshot.EffectiveWidth = Child->Width;
		Snapshot.EffectiveHeight = Child->Height;

		if (Child->Type == EPenNodeType::Ref)
		{
			if (auto Expanded = FRefExpander::ExpandRef(Child, Doc))
			{
				Snapshot.bUsedExpandedRef = true;
				if (!Child->bHasWidth)
					Snapshot.EffectiveWidth = Expanded->Width;
				if (!Child->bHasHeight)
					Snapshot.EffectiveHeight = Expanded->Height;
			}
		}
		return Snapshot;
	}

	bool ShouldSkipNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->bEnabled || IsDesignerOnlyNodeType(Node->Type))
			return true;

		// Ref 节点必须保留，后续展开后才有实际内容
		if (Node->Type == EPenNodeType::Ref)
			return false;

		// Figma 常见”占位/遮罩辅助层”：无子节点且无可渲染视觉，避免 UMG Border 默认白底污染画面。
		if (Node->Children.Num() == 0 && !HasRenderableVisual(Node))
			return true;

		return false;
	}

	/** Estimate a single node's size, handling leaf nodes (text, icon) via font metrics. */
	FVector2D EstimateNodeSize(const TSharedPtr<FDesignNode>& Node);

	FVector2D EstimateContentSize(const TSharedPtr<FDesignNode>& Node);

	FVector2D EstimateNodeSize(const TSharedPtr<FDesignNode>& Node)
	{
		FRecursionGuard Guard(GEstimateDepth, TEXT("EstimateNodeSize"));
		if (!Guard.bValid)
			return FVector2D::ZeroVector;

		float W = Node->Width.Value;
		float H = Node->Height.Value;

		// Text leaf: estimate from FontSize
		if (Node->Type == EPenNodeType::Text)
		{
			if (H == 0 && Node->FontSize > 0)
				H = Node->FontSize * 1.5f;
			if (W == 0 && Node->FontSize > 0 && !Node->Content.IsEmpty())
				W = Node->Content.Len() * Node->FontSize * 0.6f;
			return FVector2D(W, H);
		}

		// IconFont leaf: square based on FontSize
		if (Node->Type == EPenNodeType::IconFont)
		{
			const float S = Node->FontSize > 0 ? Node->FontSize : 16.0f;
			if (W == 0) W = S;
			if (H == 0) H = S;
			return FVector2D(W, H);
		}

		// Container with children: recurse
		if ((W == 0 || H == 0) && Node->Children.Num() > 0)
		{
			const FVector2D Sub(EstimateContentSize(Node));
			if (W == 0) W = Sub.X;
			if (H == 0) H = Sub.Y;
		}

		return FVector2D(W, H);
	}

	/**
	 * Estimate content size for a FitContent node by examining its children.
	 * Returns (width, height) based on layout mode and children's known sizes.
	 */
	FVector2D EstimateContentSize(const TSharedPtr<FDesignNode>& Node)
	{
		FRecursionGuard Guard(GEstimateDepth, TEXT("EstimateContentSize"));
		if (!Guard.bValid)
			return FVector2D::ZeroVector;

		float W = 0.0f, H = 0.0f;
		const float PadH = Node->Padding.Left + Node->Padding.Right;
		const float PadV = Node->Padding.Top + Node->Padding.Bottom;

		TArray<TSharedPtr<FDesignNode>> Visible;
		for (const auto& C : Node->Children)
		{
			if (!ShouldSkipNode(C))
				Visible.Add(C);
		}

		if (Visible.Num() == 0)
			return FVector2D(PadH, PadV);

		const float TotalGap = Visible.Num() > 1 ? Node->Gap * (Visible.Num() - 1) : 0.0f;

		if (Node->Layout == EPenLayout::Horizontal)
		{
			for (const auto& C : Visible)
			{
				const FVector2D CS(EstimateNodeSize(C));
				W += CS.X;
				H = FMath::Max(H, CS.Y);
			}
			W += TotalGap;
		}
		else if (Node->Layout == EPenLayout::Vertical)
		{
			for (const auto& C : Visible)
			{
				const FVector2D CS(EstimateNodeSize(C));
				W = FMath::Max(W, CS.X);
				H += CS.Y;
			}
			H += TotalGap;
		}
		else
		{
			// Absolute positioning: max extent
			for (const auto& C : Visible)
			{
				const FVector2D CS(EstimateNodeSize(C));
				W = FMath::Max(W, C->X + CS.X);
				H = FMath::Max(H, C->Y + CS.Y);
			}
		}

		return FVector2D(W + PadH, H + PadV);
	}
}

FUMGWidgetData FDesign2UMGConverter::Convert(const FDesignDocument& Document)
{
	FUMGWidgetData Result;

	// 深拷贝 Children，避免通过 TSharedPtr 修改调用者的原始数据
	TArray<TSharedPtr<FDesignNode>> Children;
	Children.Reserve(Document.Children.Num());
	for (const auto& Child : Document.Children)
		Children.Add(FRefExpander::DeepCopyNode(Child));

	// WidgetName 使用第一个非跳过 frame 的名称，fallback 到 WBP_FromPen
	for (const auto& Child : Children)
	{
		if (!ShouldSkipNode(Child) && !Child->bReusable && !Child->Name.IsEmpty())
		{
			Result.WidgetName = FString::Printf(TEXT("WBP_%s"), *Child->Name);
			break;
		}
	}
	if (Result.WidgetName.IsEmpty())
		Result.WidgetName = TEXT("WBP_FromPen");

	// 预计算 FitContent 节点的实际尺寸（从子节点递归推算）
	for (const auto& Child : Children)
	{
		if (!ShouldSkipNode(Child))
			PreComputeFitContentSizes(Child, Document);
	}

	TArray<FUMGWidgetNode> RenderWidgets;
	for (const auto& Child : Children)
	{
		if (ShouldSkipNode(Child) || Child->bReusable)
			continue;

		Child->X = 0.0f;
		Child->Y = 0.0f;

		FUMGWidgetNode ChildWidget = ConvertNode(Child, Document, TMap<FString, FString>(), &Result.Warnings);
		if (!ChildWidget.Type.IsEmpty())
			RenderWidgets.Add(MoveTemp(ChildWidget));
	}

	if (RenderWidgets.Num() == 1)
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("Convert: single root '%s' type=%s"), *RenderWidgets[0].Name, *RenderWidgets[0].Type);
		Result.RootWidget = MoveTemp(RenderWidgets[0]);
		return Result;
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("Convert: %d top-level widgets → CanvasPanel root"), RenderWidgets.Num());
	Result.RootWidget.Type = TEXT("CanvasPanel");
	Result.RootWidget.Name = TEXT("Root");
	Result.RootWidget.Id = TEXT("Root");
	for (auto& W : RenderWidgets)
		Result.RootWidget.Children.Add(MoveTemp(W));

	return Result;
}

FUMGWidgetNode FDesign2UMGConverter::ConvertNode(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc, const TMap<FString, FString>& ThemeContext, TArray<FString>* OutWarnings, EPenLayout ParentLayout)
{
	FRecursionGuard Guard(GConvertNodeDepth, TEXT("ConvertNode"));
	if (!Guard.bValid)
		return FUMGWidgetNode();

	if (ShouldSkipNode(Node))
		return FUMGWidgetNode();

	TMap<FString, FString> ActiveTheme = ThemeContext;
	for (const auto& Pair : Node->Theme)
		ActiveTheme.Add(Pair.Key, Pair.Value);

	if (Node->Type == EPenNodeType::Ref)
	{
		auto Expanded = FRefExpander::ExpandRef(Node, Doc);
		if (!Expanded.IsValid())
			return FUMGWidgetNode();

		if (ActiveTheme.Num() > 0)
		{
			FPenVariableResolver Resolver(Doc.Variables);
			Resolver.SetThemeContext(ActiveTheme);
			FRefExpander::ReThemeNode(Expanded, Resolver);
		}
		return ConvertNode(Expanded, Doc, ActiveTheme, OutWarnings, ParentLayout);
	}

	FUMGWidgetNode Widget;

	switch (Node->Type)
	{
	case EPenNodeType::Text:
		Widget = CreateTextWidget(Node);
		break;
	case EPenNodeType::IconFont:
		Widget = CreateIconFontWidget(Node);
		break;
	case EPenNodeType::Frame:
	case EPenNodeType::Group:
		Widget = CreateContainerWidget(Node);
		break;
	case EPenNodeType::Rectangle:
		Widget = CreateContainerWidget(Node);
		break;
	case EPenNodeType::Ellipse:
		{
			float W = Node->Width.IsFixed() ? Node->Width.Value : 0.0f;
			float H = Node->Height.IsFixed() ? Node->Height.Value : 0.0f;
			// 有尺寸、有颜色填充、无描边 → 生成椭圆 SVG path，走栅格化
			// 有描边时走 fallback（Border 支持 OutlineThickness，Image 不支持）
			bool bHasStroke = Node->Stroke.HasStroke();
			if (W > 0 && H > 0 && Node->Fill.IsColor() && Node->Fill.bEnabled && !bHasStroke)
			{
				float Rx = W / 2.0f, Ry = H / 2.0f;
				Node->Geometry = FString::Printf(
					TEXT("M 0,%.2f A %.2f,%.2f 0 1,1 %.2f,%.2f A %.2f,%.2f 0 1,1 0,%.2f Z"),
					Ry, Rx, Ry, W, Ry, Rx, Ry, Ry);
				Widget = CreatePathWidget(Node);
			}
			else
			{
				// Fallback: 圆角近似
				Widget = CreateContainerWidget(Node);
				if (W > 0 && H > 0)
				{
					FPenCornerRadius EllipseRadius;
					EllipseRadius.bUniform = true;
					EllipseRadius.Uniform = FMath::Min(W, H) / 2.0f;
					FVisualMapper::ApplyRoundedCorner(Widget, EllipseRadius);
				}
			}
		}
		break;
	case EPenNodeType::Polygon:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Design2UMG: Polygon '%s' mapped to Border (UMG has no polygon widget)"), *Node->Name);
		UE_LOG(LogDesign2UMG, Warning, TEXT("多边形 '%s' 已转换为 Border（UMG 不支持多边形控件）"), *Node->Name);
		if (OutWarnings)
			OutWarnings->Add(FString::Printf(TEXT("Polygon '%s' → Border (UMG 无多边形控件)"), *Node->Name));
		break;
	case EPenNodeType::Path:
		Widget = CreatePathWidget(Node);
		break;
	case EPenNodeType::Line:
		Widget = CreateContainerWidget(Node);
		UE_LOG(LogDesign2UMG, Verbose, TEXT("Design2UMG: Line '%s' mapped to Image (UMG has no line widget)"), *Node->Name);
		UE_LOG(LogDesign2UMG, Warning, TEXT("线条 '%s' 已转换为 Image（UMG 不支持线条控件）"), *Node->Name);
		if (OutWarnings)
			OutWarnings->Add(FString::Printf(TEXT("Line '%s' → Image (UMG 无线条控件)"), *Node->Name));
		break;
	default:
		Widget = CreateContainerWidget(Node);
		break;
	}

	Widget.Id = Node->Id;
	Widget.Name = Node->Name.IsEmpty() ? Node->Id : Node->Name;
	Widget.Bounds.X = Node->X;
	Widget.Bounds.Y = Node->Y;
	Widget.Bounds.W = Node->Width.Value;
	Widget.Bounds.H = Node->Height.Value;

	UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' id=%s type=%s bounds=(%.1f,%.1f,%.1f,%.1f) layout=%d children=%d"),
		*Widget.Name, *Widget.Id, *Widget.Type,
		Widget.Bounds.X, Widget.Bounds.Y, Widget.Bounds.W, Widget.Bounds.H,
		(int32)Node->Layout, Node->Children.Num());

	FVisualMapper::ApplyVisualProperties(Widget, Node);
	if (Widget.Type == TEXT("Border")
		&& !Widget.Properties.Contains(TEXT("BrushColor"))
		&& !Widget.Properties.Contains(TEXT("TexturePath"))
		&& !Widget.Properties.Contains(TEXT("MaterialRef")))
	{
		// Stroke-only / no-fill Border should keep transparent interior.
		// UBorder default brush is opaque white, which can cover siblings.
		Widget.Properties.Add(TEXT("BrushColor"), TEXT("#00000000"));
	}

	// 预处理：在递归子节点之前，解析 FillContainer 尺寸写入 PenNode
	// 这样子节点递归时 Width.Value / Height.Value 已经是正确的像素值
	// 直接修改原始子节点（Ref 节点的 Width/Height 会被 ExpandRef 传递给展开节点）
	if (Widget.Bounds.W > 0 && Widget.Bounds.H > 0
		&& (Node->Layout == EPenLayout::Horizontal || Node->Layout == EPenLayout::Vertical))
	{
		const float PadL = Node->Padding.Left, PadR = Node->Padding.Right;
		const float PadT = Node->Padding.Top, PadB = Node->Padding.Bottom;
		const float InnerW = Widget.Bounds.W - PadL - PadR;
		const float InnerH = Widget.Bounds.H - PadT - PadB;
		const bool bHorizontal = (Node->Layout == EPenLayout::Horizontal);

		// Build effective size snapshots (ref children use expanded mode/value for stats).
		TArray<FChildSizeSnapshot> EffChildren;
		for (const auto& Child : Node->Children)
		{
			if (!ShouldSkipNode(Child))
				EffChildren.Add(BuildChildSizeSnapshot(Child, Doc));
		}

		// 统计主轴 Fixed 占用和 Fill 数量
		float FixedMainSum = 0.0f;
		int32 FillCount = 0;
		const float TotalGap = EffChildren.Num() > 1 ? Node->Gap * (EffChildren.Num() - 1) : 0.0f;
		for (const auto& EC : EffChildren)
		{
			const FPenSize& MainSize = bHorizontal ? EC.EffectiveWidth : EC.EffectiveHeight;
			if (MainSize.IsFill())
				FillCount++;
			else
				FixedMainSum += MainSize.Value;

			const FPenSize& RawMain = bHorizontal ? EC.RawWidth : EC.RawHeight;
			const FPenSize& RawCross = bHorizontal ? EC.RawHeight : EC.RawWidth;
			const FPenSize& EffectiveCross = bHorizontal ? EC.EffectiveHeight : EC.EffectiveWidth;
			UE_LOG(LogDesign2UMG, Verbose, TEXT("PreResolveFill Child '%s': ref=%d rawMain=%s(%.0f) effMain=%s(%.0f) rawCross=%s(%.0f) effCross=%s(%.0f)"),
				EC.Child.IsValid() ? *EC.Child->Id : TEXT("<invalid>"),
				EC.bUsedExpandedRef ? 1 : 0,
				ToSizeModeLogString(RawMain.Mode), RawMain.Value,
				ToSizeModeLogString(MainSize.Mode), MainSize.Value,
				ToSizeModeLogString(RawCross.Mode), RawCross.Value,
				ToSizeModeLogString(EffectiveCross.Mode), EffectiveCross.Value);
		}

		// 辅助 lambda：只修改 Value 不改 Mode，保留 FillContainer 语义给下游 Slot 映射
		// 同时设置 bHas 标志确保 Ref 节点的 ExpandRef 传递
		auto ResolveSize = [](TSharedPtr<FDesignNode>& N, bool bWidth, float Px)
		{
			FPenSize& Size = bWidth ? N->Width : N->Height;
			Size.Value = FMath::Max(0.0f, Px);
			if (bWidth) N->bHasWidth = true;
			else N->bHasHeight = true;
		};

		if (FillCount > 0)
		{
			const float AvailMain = (bHorizontal ? InnerW : InnerH) - FixedMainSum - TotalGap;
			const float FillMain = FMath::Max(0.0f, AvailMain / FillCount);

			for (auto& EC : EffChildren)
			{
				const FPenSize& MainSize = bHorizontal ? EC.EffectiveWidth : EC.EffectiveHeight;
				if (MainSize.IsFill())
					ResolveSize(EC.Child, bHorizontal, FillMain);
				const FPenSize& CrossSize = bHorizontal ? EC.EffectiveHeight : EC.EffectiveWidth;
				if (CrossSize.IsFill())
					ResolveSize(EC.Child, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFill: '%s' layout=%s inner=(%.0f,%.0f) fixed=%.0f fill=%d each=%.0f"),
				*Widget.Name, bHorizontal ? TEXT("H") : TEXT("V"),
				InnerW, InnerH, FixedMainSum, FillCount, FillMain);
		}
		else
		{
			// 没有 Fill 子节点，但交叉轴可能有 FillContainer
			for (auto& EC : EffChildren)
			{
				const FPenSize& CrossSize = bHorizontal ? EC.EffectiveHeight : EC.EffectiveWidth;
				if (CrossSize.IsFill())
					ResolveSize(EC.Child, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}
		}
	}

	// Pre-compute FitContent sizes: for children with FitContent+Value=0,
	// estimate content size from their subtree so they don't collapse to zero.
	for (auto& Child : Node->Children)
	{
		if (ShouldSkipNode(Child))
			continue;

		auto ResolveFit = [](TSharedPtr<FDesignNode>& N, bool bWidth, float Px)
		{
			FPenSize& Size = bWidth ? N->Width : N->Height;
			if (Size.IsFit() && Size.Value == 0.0f && Px > 0.0f)
			{
				Size.Value = Px;
				if (bWidth) N->bHasWidth = true;
				else N->bHasHeight = true;
			}
		};

		if ((Child->Width.IsFit() && Child->Width.Value == 0.0f)
			|| (Child->Height.IsFit() && Child->Height.Value == 0.0f))
		{
			const FVector2D Est(EstimateContentSize(Child));
			ResolveFit(Child, true, Est.X);
			ResolveFit(Child, false, Est.Y);

			if (Est.X > 0 || Est.Y > 0)
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFitContent: '%s' estimated=(%.0f,%.0f)"),
					*Child->Name, Est.X, Est.Y);
			}
		}
	}

	for (const auto& Child : Node->Children)
	{
		if (ShouldSkipNode(Child))
			continue;

		TSharedPtr<FDesignNode> EffectiveChild = Child;
		if (Child->Type == EPenNodeType::Ref)
		{
			auto Expanded = FRefExpander::ExpandRef(Child, Doc);
			if (Expanded.IsValid())
			{
				if (ActiveTheme.Num() > 0)
				{
					FPenVariableResolver Resolver(Doc.Variables);
					Resolver.SetThemeContext(ActiveTheme);
					FRefExpander::ReThemeNode(Expanded, Resolver);
				}
				EffectiveChild = Expanded;
			}
		}

		auto ChildWidget = ConvertNode(EffectiveChild, Doc, ActiveTheme, OutWarnings, Node->Layout);
		if (ChildWidget.Type.IsEmpty())
			continue;

		FLayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->Layout);
		FLayoutMapper::ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);

		const bool bHasFixedSize = EffectiveChild->Width.IsFixed() || EffectiveChild->Height.IsFixed();
		const bool bHasMinSize = (EffectiveChild->Width.IsFit() && EffectiveChild->Width.Value > 0)
			|| (EffectiveChild->Height.IsFit() && EffectiveChild->Height.Value > 0);
		// Canvas children with mixed FitContent+Fixed axes need SizeBox to enforce the Fixed axis
		const bool bCanvasMixedAxis = Node->Layout == EPenLayout::None
			&& ((EffectiveChild->Width.IsFit() && EffectiveChild->Height.IsFixed())
				|| (EffectiveChild->Height.IsFit() && EffectiveChild->Width.IsFixed()));
		// CanvasPanel 的尺寸本来就由 Slot 偏移控制；给“普通 fixed 子节点”再套 SizeBox
		// 会把内部控件退化为默认 100x100 期望尺寸，导致大面积错位/压扁。
		bool bNeedsSizeBox = false;
		if (Node->Layout == EPenLayout::None)
		{
			bNeedsSizeBox = bCanvasMixedAxis;
		}
		else
		{
			bNeedsSizeBox = (bHasFixedSize || bHasMinSize);
		}
		if (bNeedsSizeBox)
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("  child '%s' → SizeBox wrap (fixed=%d min=%d w=%.1f h=%.1f)"),
				*ChildWidget.Name, bHasFixedSize, bHasMinSize,
				EffectiveChild->Width.Value, EffectiveChild->Height.Value);
			Widget.Children.Add(FLayoutMapper::WrapWithSizeBox(MoveTemp(ChildWidget), EffectiveChild));
		}
		else
			Widget.Children.Add(MoveTemp(ChildWidget));
	}

	if (Node->Gap > 0)
		FLayoutMapper::ApplyGap(Widget.Children, Node->Gap, Node->Layout);

	if (Node->JustifyContent != EPenJustifyContent::Start)
	{
		// Determine the container's main-axis size mode for Spacer suppression.
		// ParentLayout tells us if this container sits inside a CanvasPanel.
		const EPenSizeMode MainAxisMode = (Node->Layout == EPenLayout::Horizontal)
			? Node->Width.Mode : Node->Height.Mode;
		FLayoutMapper::ApplyJustifyContent(Widget.Children, Node->JustifyContent, MainAxisMode, ParentLayout);
	}

	const bool bHasVisualFill = (Node->Fill.bEnabled && Node->Fill.Type != EPenFillType::None)
		|| Node->Fills.Num() > 0;
	const bool bHasCornerRadius = Node->CornerRadius.bUniform ? Node->CornerRadius.Uniform > 0
		: (Node->CornerRadius.TopLeft > 0 || Node->CornerRadius.TopRight > 0
			|| Node->CornerRadius.BottomRight > 0 || Node->CornerRadius.BottomLeft > 0);
	bool bNeedsBorder = (Node->Type == EPenNodeType::Frame || Node->Type == EPenNodeType::Group)
		&& (bHasVisualFill || FLayoutMapper::HasPadding(Node) || bHasCornerRadius || Node->Stroke.HasStroke());

	if (bNeedsBorder)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
			*Widget.Name, bHasVisualFill, bHasCornerRadius, Node->Stroke.HasStroke());
		Widget = FVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	}
	else if (FLayoutMapper::HasPadding(Node))
		FLayoutMapper::ApplyPadding(Widget, Node->Padding);

	// 多层填充叠加（Overlay 包裹）
	if (Node->HasMultiFill())
		Widget = FVisualMapper::WrapWithMultiFill(MoveTemp(Widget), Node);

	if (Widget.Properties.Contains(TEXT("BoxShadowOffsetX")))
		Widget = FVisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node);

	for (const FPenEffect& Effect : Node->Effects)
	{
		if (Effect.bEnabled && Effect.Type == EPenEffectType::BackgroundBlur && Effect.Radius > 0)
			return FVisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), Effect.Radius);
	}

	return Widget;
}

FUMGWidgetNode FDesign2UMGConverter::CreateContainerWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = FPenTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}

FUMGWidgetNode FDesign2UMGConverter::CreateTextWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");
	Widget.Properties.Add(TEXT("Text"), Node->Content);
	ApplyTextProperties(Widget, Node);
	return Widget;
}

void FDesign2UMGConverter::ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	if (Node->FontSize > 0)
	{
		Widget.Properties.Add(TEXT("FontSize"), FString::SanitizeFloat(Node->FontSize));
		Widget.TextStyle.FontSize = Node->FontSize;
	}

	if (!Node->FontFamily.IsEmpty())
	{
		FString AssetPath = FFontResolver::FindFontAssetPath(Node->FontFamily);
		Widget.Properties.Add(TEXT("FontFamily"), AssetPath.IsEmpty() ? Node->FontFamily : AssetPath);
	}

	if (!Node->FontWeight.IsEmpty())
		Widget.Properties.Add(TEXT("TypefaceFontName"), FPenTypeMapper::MapFontWeight(Node->FontWeight).ToString());

	Widget.Properties.Add(TEXT("Justification"),
		Node->TextAlign == EPenTextAlign::Center ? TEXT("Center") :
		Node->TextAlign == EPenTextAlign::Right ? TEXT("Right") : TEXT("Left"));

	if (Node->LineHeight > 0)
		Widget.Properties.Add(TEXT("LineHeightPercentage"), FString::SanitizeFloat(Node->LineHeight));

	if (Node->LetterSpacing != 0)
	{
		Widget.Properties.Add(TEXT("LetterSpacing"), FString::SanitizeFloat(Node->LetterSpacing));
		Widget.TextStyle.LetterSpacing = Node->LetterSpacing;
		Widget.TextStyle.bHasLetterSpacing = true;
	}

	switch (Node->TextGrowth)
	{
	case EPenTextGrowth::FixedWidth:
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		break;
	case EPenTextGrowth::FixedWidthHeight:
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));
		break;
	default:
		break;
	}

	if (Node->TextAlignVertical != EPenTextAlignVertical::Top)
	{
		Widget.Properties.Add(TEXT("TextAlignVertical"),
			Node->TextAlignVertical == EPenTextAlignVertical::Middle ? TEXT("Center") : TEXT("Bottom"));
	}
}

FUMGWidgetNode FDesign2UMGConverter::CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");

	UDesign2UMGSettings* MutableSettings = UDesign2UMGSettings::Get();
	const UDesign2UMGSettings* Settings = MutableSettings;
	FString AssetPath;
	bool bUsedDefaultIconPath = false;
	if (Settings && !Node->IconFontFamily.IsEmpty())
	{
		AssetPath = Settings->FindIconFontAssetPath(Node->IconFontFamily);
		if (AssetPath.IsEmpty())
		{
			AssetPath = Settings->DefaultIconFontAssetPath;
			bUsedDefaultIconPath = !AssetPath.IsEmpty();
		}
	}

	// If family mapping is missing but a valid default icon font path exists, persist it once.
	if (bUsedDefaultIconPath && MutableSettings && !Node->IconFontFamily.IsEmpty())
	{
		const TSoftObjectPtr<UFont>* Existing = MutableSettings->IconFontAssetMap.Find(Node->IconFontFamily);
		if (!Existing || Existing->IsNull())
		{
			MutableSettings->IconFontAssetMap.Add(Node->IconFontFamily, TSoftObjectPtr<UFont>(FSoftObjectPath(AssetPath)));
			UE_LOG(LogDesign2UMG, Display,
				TEXT("Auto-mapped icon font family '%s' to default asset path '%s'"),
				*Node->IconFontFamily, *AssetPath);
		}
	}

	if (!AssetPath.IsEmpty())
	{
		Widget.Properties.Add(TEXT("FontFamily"), AssetPath);
	}
	else if (!Node->IconFontFamily.IsEmpty())
	{
		static TSet<FString> WarnedFamilies;
		if (!WarnedFamilies.Contains(Node->IconFontFamily))
		{
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("IconFont '%s': font family '%s' has no asset mapping"),
				*Node->IconFontName, *Node->IconFontFamily);
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("图标字体 '%s' (字体族 '%s') 未配置资产映射。请在 编辑器偏好设置 > 插件 > Pen To UMG 中配置。"),
				*Node->IconFontName, *Node->IconFontFamily);
			WarnedFamilies.Add(Node->IconFontFamily);
		}
	}

	// 图标名称即连字文本（如 "dashboard" 在 Material Symbols 中渲染为图标）
	Widget.Properties.Add(TEXT("IconName"), Node->IconFontName);
	FString IconText;
	if (!AssetPath.IsEmpty())
	{
		IconText = Node->IconFontName;
	}
	else if (Settings && !Settings->MissingIconFontPlaceholder.IsEmpty())
	{
		IconText = Settings->MissingIconFontPlaceholder;
	}
	if (!IconText.IsEmpty())
		Widget.Properties.Add(TEXT("Text"), IconText);

	if (Node->FontSize > 0)
	{
		Widget.Properties.Add(TEXT("FontSize"), FString::SanitizeFloat(Node->FontSize));
		Widget.TextStyle.FontSize = Node->FontSize;
	}

	if (Node->Fill.Type == EPenFillType::Color)
		Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);

	return Widget;
}

FUMGWidgetNode FDesign2UMGConverter::CreatePathWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;

	// 已被 FigmaImageExporter 转为 Image Fill → 走 Image 路径
	if (Node->Fill.Type == EPenFillType::Image && !Node->Fill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
		if (Node->Fill.Image.Mode == EPenImageMode::Crop && !Node->Fill.Image.Transform.IsIdentity())
		{
			const FPenImageTransform& T = Node->Fill.Image.Transform;
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Crop"));
			Widget.Properties.Add(TEXT("ImageTransform"),
				FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
					T.A, T.B, T.TX, T.C, T.D, T.TY));
		}
		else if (Node->Fill.Image.Mode == EPenImageMode::Fill)
		{
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fill"));
		}
		else if (Node->Fill.Image.Mode == EPenImageMode::Fit)
		{
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fit"));
		}
		if (Node->Fill.Image.Opacity < 1.0f)
		{
			Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Node->Fill.Image.Opacity));
		}
		int32 W = FMath::Max(1, FMath::RoundToInt(Node->Width.Value));
		int32 H = FMath::Max(1, FMath::RoundToInt(Node->Height.Value));
		Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt(W));
		Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt(H));
		return Widget;
	}

	// 有 geometry 数据 → 栅格化（fill 优先，stroke-only 回退用 stroke 颜色填充）
	if (!Node->Geometry.IsEmpty())
	{
		FString RasterColor;
		if (Node->Fill.Type == EPenFillType::Color)
			RasterColor = Node->Fill.SolidColor;
		else if (Node->Fill.Type == EPenFillType::Gradient && Node->Fill.Gradient.Colors.Num() > 0)
			RasterColor = Node->Fill.Gradient.Colors[0].Color;
		else if (Node->Stroke.HasStroke() && Node->Stroke.Fill.Type == EPenFillType::Color)
			RasterColor = Node->Stroke.Fill.SolidColor;
		else
		{
			for (const FPenFill& Fill : Node->Fills)
			{
				if (Fill.Type == EPenFillType::Color)
				{
					RasterColor = Fill.SolidColor;
					break;
				}
				if (Fill.Type == EPenFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
				{
					RasterColor = Fill.Gradient.Colors[0].Color;
					break;
				}
			}
		}

		if (!RasterColor.IsEmpty())
		{
			int32 W = FMath::Max(1, FMath::RoundToInt(Node->Width.Value));
			int32 H = FMath::Max(1, FMath::RoundToInt(Node->Height.Value));

			UE_LOG(LogDesign2UMG, Log, TEXT("CreatePathWidget: '%s' rasterize %dx%d (raw=%.1fx%.1f) geometry=%d chars"),
				*Node->Name, W, H, Node->Width.Value, Node->Height.Value, Node->Geometry.Len());

			Widget.Type = TEXT("Image");
			Widget.Properties.Add(TEXT("PathGeometry"), Node->Geometry);
			Widget.Properties.Add(TEXT("PathFillRule"), Node->FillRule);
			Widget.Properties.Add(TEXT("PathFillColor"), RasterColor);
			Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt(W));
			Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt(H));
			return Widget;
		}
	}

	// Fallback
	Widget.Type = FPenTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}

FVector2D FDesign2UMGConverter::PreComputeFitContentSizes(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc)
{
	FRecursionGuard Guard(GPrecomputeDepth, TEXT("PreComputeFitContentSizes"));
	if (!Guard.bValid)
		return FVector2D::ZeroVector;

	if (!Node.IsValid() || !Node->bEnabled)
	{
		return FVector2D(0.0, 0.0);
	}

	// Ref 节点：展开后递归，回写尺寸到原始节点
	if (Node->Type == EPenNodeType::Ref)
	{
		auto Expanded = FRefExpander::ExpandRef(Node, Doc);
		if (Expanded.IsValid())
		{
			const FVector2D RefSize(PreComputeFitContentSizes(Expanded, Doc));
			if (Node->Width.IsFit() && Node->Width.Value == 0 && RefSize.X > 0 && !Expanded->Width.IsFill())
				Node->Width.Value = static_cast<float>(RefSize.X);
			if (Node->Height.IsFit() && Node->Height.Value == 0 && RefSize.Y > 0 && !Expanded->Height.IsFill())
				Node->Height.Value = static_cast<float>(RefSize.Y);
			return RefSize;
		}
		return FVector2D(0.0, 0.0);
	}

	// Text 叶子节点：用 FontSize 估算
	if (Node->Type == EPenNodeType::Text)
	{
		float W = Node->Width.IsFixed() ? Node->Width.Value : 0.0f;
		float H = Node->Height.IsFixed() ? Node->Height.Value : 0.0f;
		if (H == 0 && Node->FontSize > 0)
			H = Node->FontSize * 1.5f;
		if (W == 0 && Node->FontSize > 0 && !Node->Content.IsEmpty())
			W = Node->Content.Len() * Node->FontSize * 0.6f;
		// 回写估算值，使 ConvertNode 的 Bounds.W/H 非零
		if (W > 0 && !Node->Width.IsFixed())
			Node->Width.Value = W;
		if (H > 0 && !Node->Height.IsFixed())
			Node->Height.Value = H;
		return FVector2D(W, H);
	}

	// IconFont 叶子节点
	if (Node->Type == EPenNodeType::IconFont)
	{
		float S = Node->FontSize > 0 ? Node->FontSize : 16.0f;
		if (!Node->Width.IsFixed())
			Node->Width.Value = S;
		if (!Node->Height.IsFixed())
			Node->Height.Value = S;
		return FVector2D(S, S);
	}

	// 矢量叶子节点（Path/Line/Ellipse/Polygon）：亚像素钳制到 1px 并回写
	// CreatePathWidget 用 Max(1, RoundToInt) 算 WidthOverride，这里对齐
	const bool bIsVectorLeaf = Node->Type == EPenNodeType::Path
		|| Node->Type == EPenNodeType::Line
		|| Node->Type == EPenNodeType::Ellipse
		|| Node->Type == EPenNodeType::Polygon;
	if (bIsVectorLeaf)
	{
		float W = FMath::Max(1.0f, FMath::RoundToFloat(Node->Width.Value));
		float H = FMath::Max(1.0f, FMath::RoundToFloat(Node->Height.Value));
		if (!Node->Width.IsFixed() || Node->Width.Value < 1.0f)
			Node->Width.Value = W;
		if (!Node->Height.IsFixed() || Node->Height.Value < 1.0f)
			Node->Height.Value = H;
		return FVector2D(W, H);
	}

	// 其他非容器叶子节点
	if (!Node->IsContainer() && Node->Type != EPenNodeType::Rectangle)
	{
		return FVector2D(
			Node->Width.IsFixed() ? Node->Width.Value : 0.0f,
			Node->Height.IsFixed() ? Node->Height.Value : 0.0f);
	}

	// 容器节点：先递归子节点
	for (const auto& Child : Node->Children)
	{
		(void)PreComputeFitContentSizes(Child, Doc);
	}

	// 如果本节点有 FitContent 轴且 Value=0，用 EstimateContentSize 回填
	const bool bNeedW = Node->Width.IsFit() && Node->Width.Value == 0;
	const bool bNeedH = Node->Height.IsFit() && Node->Height.Value == 0;

	float FinalW = Node->Width.Value;
	float FinalH = Node->Height.Value;

	if (bNeedW || bNeedH)
	{
		const FVector2D Est(EstimateContentSize(Node));
		if (bNeedW && Est.X > 0)
		{
			Node->Width.Value = static_cast<float>(Est.X);
			FinalW = static_cast<float>(Est.X);
			UE_LOG(LogDesign2UMG, Log, TEXT("PreCompute: '%s' width fit→%.0f"), *Node->Name, Est.X);
		}
		if (bNeedH && Est.Y > 0)
		{
			Node->Height.Value = static_cast<float>(Est.Y);
			FinalH = static_cast<float>(Est.Y);
			UE_LOG(LogDesign2UMG, Log, TEXT("PreCompute: '%s' height fit→%.0f"), *Node->Name, Est.Y);
		}
	}

	return FVector2D(FinalW, FinalH);
}
