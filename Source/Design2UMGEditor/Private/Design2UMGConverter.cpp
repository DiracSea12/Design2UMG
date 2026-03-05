// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "DesignTypeMapper.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "FontResolver.h"
#include "LayoutMapper.h"
#include "VisualMapper.h"
#include "RefExpander.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Engine/Font.h"
#include "Misc/PackageName.h"

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

	bool IsDesignerOnlyNodeType(EDesignNodeType Type)
	{
		return Type == EDesignNodeType::Connection
			|| Type == EDesignNodeType::Note
			|| Type == EDesignNodeType::Prompt
			|| Type == EDesignNodeType::Context;
	}

	bool HasVisibleFill(const FDesignFill& Fill)
	{
		return Fill.bEnabled && Fill.Type != EDesignFillType::None;
	}

	bool HasRenderableVisual(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
			return false;

		if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			return true;

		if (HasVisibleFill(Node->Fill))
			return true;

		for (const FDesignFill& Fill : Node->Fills)
		{
			if (HasVisibleFill(Fill))
				return true;
		}

		if (Node->Stroke.HasStroke())
			return true;

		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (Effect.bEnabled)
				return true;
		}

		return false;
	}

	FString GetSourceExtension(const TSharedPtr<FDesignNode>& Node, const FString& Key)
	{
		if (!Node.IsValid())
			return FString();
		if (const FString* Value = Node->Metadata.Find(Key))
			return *Value;
		return FString();
	}

	bool TryNormalizeValidFontAssetPath(const FString& InPath, FString& OutPath)
	{
		OutPath = InPath.TrimStartAndEnd();
		if (OutPath.IsEmpty())
		{
			return false;
		}

		if (LoadObject<UFont>(nullptr, *OutPath))
		{
			return true;
		}

		if (!OutPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPackageName::GetLongPackageAssetName(OutPath);
			if (!AssetName.IsEmpty())
			{
				const FString CandidateObjectPath = OutPath + TEXT(".") + AssetName;
				if (LoadObject<UFont>(nullptr, *CandidateObjectPath))
				{
					OutPath = CandidateObjectPath;
					return true;
				}
			}
		}

		return false;
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

		if (Child->Type == EDesignNodeType::Ref)
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
		if (Node->Type == EDesignNodeType::Ref)
			return false;

		// Keep explicit flow containers (including empty GRID/WRAP/H/V frames) so layout semantics are not dropped.
		if (Node->Children.Num() == 0 && !HasRenderableVisual(Node))
		{
			const bool bIsExplicitFlowContainer =
				(Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
				&& Node->Layout != EDesignLayout::None;
			if (!bIsExplicitFlowContainer)
				return true;
		}

		return false;
	}

	/** Use FSlateFontMeasure for precise text size; falls back to heuristic. */
	FVector2D MeasureTextSize(const FString& Text, float FontSize, const FString& FontFamily, const FString& FontWeight)
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateRenderer* Renderer = FSlateApplication::Get().GetRenderer();
			if (Renderer)
			{
				TSharedRef<FSlateFontMeasure> Measure = Renderer->GetFontMeasureService();
				FSlateFontInfo FontInfo;
				FontInfo.Size = FontSize;
				if (!FontFamily.IsEmpty())
				{
					const FString AssetPath = FFontResolver::FindFontAssetPath(FontFamily);
					if (!AssetPath.IsEmpty())
					{
						if (UObject* FontObject = LoadObject<UObject>(nullptr, *AssetPath))
						{
							FontInfo.FontObject = FontObject;
						}
					}
					const FString Typeface = FFontResolver::FindBestTypeface(FontFamily, FontWeight);
					if (!Typeface.IsEmpty())
					{
						FontInfo.TypefaceFontName = FName(*Typeface);
					}
				}
				FVector2D Size = Measure->Measure(Text, FontInfo);
				if (Size.X > 0 && Size.Y > 0)
					return Size;
			}
		}
		// Fallback: heuristic
		float W = Text.Len() * FontSize * 0.6f;
		float H = FontSize * 1.5f;
		return FVector2D(W, H);
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

		// Text leaf: use FSlateFontMeasure when available
		if (Node->Type == EDesignNodeType::Text)
		{
			if ((W == 0 || H == 0) && Node->FontSize > 0 && !Node->Content.IsEmpty())
			{
				FVector2D Measured = MeasureTextSize(Node->Content, Node->FontSize, Node->FontFamily, Node->FontWeight);
				if (W == 0) W = Measured.X;
				if (H == 0) H = Measured.Y;
			}
			else
			{
				if (H == 0 && Node->FontSize > 0)
					H = Node->FontSize * 1.5f;
			}
			return FVector2D(W, H);
		}

		// IconFont leaf: square based on FontSize
		if (Node->Type == EDesignNodeType::IconFont)
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

		if (Node->Layout == EDesignLayout::Horizontal)
		{
			for (const auto& C : Visible)
			{
				const FVector2D CS(EstimateNodeSize(C));
				W += CS.X;
				H = FMath::Max(H, CS.Y);
			}
			W += TotalGap;
		}
		else if (Node->Layout == EDesignLayout::Vertical)
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

FUMGWidgetNode FDesign2UMGConverter::ConvertNode(const TSharedPtr<FDesignNode>& Node, const FDesignDocument& Doc, const TMap<FString, FString>& ThemeContext, TArray<FString>* OutWarnings, EDesignLayout ParentLayout)
{
	FRecursionGuard Guard(GConvertNodeDepth, TEXT("ConvertNode"));
	if (!Guard.bValid)
		return FUMGWidgetNode();

	if (ShouldSkipNode(Node))
		return FUMGWidgetNode();

	TMap<FString, FString> ActiveTheme = ThemeContext;
	for (const auto& Pair : Node->Theme)
		ActiveTheme.Add(Pair.Key, Pair.Value);

	if (Node->Type == EDesignNodeType::Ref)
	{
		auto Expanded = FRefExpander::ExpandRef(Node, Doc);
		if (!Expanded.IsValid())
			return FUMGWidgetNode();

		if (ActiveTheme.Num() > 0)
		{
			FDesignVariableResolver Resolver(Doc.Variables);
			Resolver.SetThemeContext(ActiveTheme);
			FRefExpander::ReThemeNode(Expanded, Resolver);
		}
		// Expanded refs are ephemeral copies; precompute fit-content on this copy
		// so nested text/icon bounds don't collapse to zero in UMG designer.
		PreComputeFitContentSizes(Expanded, Doc);
		return ConvertNode(Expanded, Doc, ActiveTheme, OutWarnings, ParentLayout);
	}

	FUMGWidgetNode Widget;

	switch (Node->Type)
	{
	case EDesignNodeType::Text:
		Widget = CreateTextWidget(Node);
		break;
	case EDesignNodeType::IconFont:
		Widget = CreateIconFontWidget(Node);
		break;
	case EDesignNodeType::Frame:
	case EDesignNodeType::Group:
		Widget = CreateContainerWidget(Node);
		break;
	case EDesignNodeType::Rectangle:
		Widget = CreateContainerWidget(Node);
		break;
	case EDesignNodeType::Ellipse:
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
					FDesignCornerRadius EllipseRadius;
					EllipseRadius.bUniform = true;
					EllipseRadius.Uniform = FMath::Min(W, H) / 2.0f;
					FVisualMapper::ApplyRoundedCorner(Widget, EllipseRadius);
				}
			}
		}
		break;
	case EDesignNodeType::Polygon:
		if (!Node->Geometry.IsEmpty())
		{
			Widget = CreatePathWidget(Node);
		}
		else
		{
			Widget = CreateContainerWidget(Node);
			UE_LOG(LogDesign2UMG, Warning, TEXT("多边形 '%s' 无 PathGeometry，降级为 Border"), *Node->Name);
			if (OutWarnings)
				OutWarnings->Add(FString::Printf(TEXT("Polygon '%s' → Border (无 PathGeometry)"), *Node->Name));
		}
		break;
	case EDesignNodeType::Path:
		Widget = CreatePathWidget(Node);
		break;
	case EDesignNodeType::Line:
		if (!Node->Geometry.IsEmpty())
		{
			Widget = CreatePathWidget(Node);
		}
		else
		{
			// 无 Geometry 时用 Width/Height 构造水平线段
			float W = FMath::Max(1.0f, Node->Width.Value);
			float H = FMath::Max(1.0f, Node->Height.Value);
			Node->Geometry = FString::Printf(TEXT("M 0,%.1f L %.1f,%.1f"), H * 0.5f, W, H * 0.5f);
			Widget = CreatePathWidget(Node);
		}
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
		&& (Node->Layout == EDesignLayout::Horizontal || Node->Layout == EDesignLayout::Vertical))
	{
		const float PadL = Node->Padding.Left, PadR = Node->Padding.Right;
		const float PadT = Node->Padding.Top, PadB = Node->Padding.Bottom;
		const float InnerW = Widget.Bounds.W - PadL - PadR;
		const float InnerH = Widget.Bounds.H - PadT - PadB;
		const bool bHorizontal = (Node->Layout == EDesignLayout::Horizontal);

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
			const FDesignSize& MainSize = bHorizontal ? EC.EffectiveWidth : EC.EffectiveHeight;
			if (MainSize.IsFill())
				FillCount++;
			else
				FixedMainSum += MainSize.Value;

			const FDesignSize& RawMain = bHorizontal ? EC.RawWidth : EC.RawHeight;
			const FDesignSize& RawCross = bHorizontal ? EC.RawHeight : EC.RawWidth;
			const FDesignSize& EffectiveCross = bHorizontal ? EC.EffectiveHeight : EC.EffectiveWidth;
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
			FDesignSize& Size = bWidth ? N->Width : N->Height;
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
				const FDesignSize& MainSize = bHorizontal ? EC.EffectiveWidth : EC.EffectiveHeight;
				if (MainSize.IsFill())
					ResolveSize(EC.Child, bHorizontal, FillMain);
				const FDesignSize& CrossSize = bHorizontal ? EC.EffectiveHeight : EC.EffectiveWidth;
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
				const FDesignSize& CrossSize = bHorizontal ? EC.EffectiveHeight : EC.EffectiveWidth;
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
			FDesignSize& Size = bWidth ? N->Width : N->Height;
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
		if (Child->Type == EDesignNodeType::Ref)
		{
			auto Expanded = FRefExpander::ExpandRef(Child, Doc);
			if (Expanded.IsValid())
			{
				if (ActiveTheme.Num() > 0)
				{
					FDesignVariableResolver Resolver(Doc.Variables);
					Resolver.SetThemeContext(ActiveTheme);
					FRefExpander::ReThemeNode(Expanded, Resolver);
				}
				// Ensure expanded ref subtree has concrete fit-content sizes before converting.
				PreComputeFitContentSizes(Expanded, Doc);
				EffectiveChild = Expanded;
			}
		}

		auto ChildWidget = ConvertNode(EffectiveChild, Doc, ActiveTheme, OutWarnings, Node->Layout);
		if (ChildWidget.Type.IsEmpty())
			continue;

		FLayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->Layout);
		FLayoutMapper::ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);
		if (Node->Layout != EDesignLayout::None
			&& !EffectiveChild->bAbsoluteLayoutPositioning)
		{
			// Flow containers use slot sequencing; child local bounds origin should stay at (0,0).
			ChildWidget.Bounds.X = 0.0f;
			ChildWidget.Bounds.Y = 0.0f;
		}

		const bool bHasFixedSize = EffectiveChild->Width.IsFixed() || EffectiveChild->Height.IsFixed();
		const bool bHasMinSize = (EffectiveChild->Width.IsFit() && EffectiveChild->Width.Value > 0)
			|| (EffectiveChild->Height.IsFit() && EffectiveChild->Height.Value > 0);
		// Canvas children with mixed FitContent+Fixed axes need SizeBox to enforce the Fixed axis
		const bool bCanvasMixedAxis = Node->Layout == EDesignLayout::None
			&& ((EffectiveChild->Width.IsFit() && EffectiveChild->Height.IsFixed())
				|| (EffectiveChild->Height.IsFit() && EffectiveChild->Width.IsFixed()));
		// CanvasPanel 的尺寸本来就由 Slot 偏移控制；给“普通 fixed 子节点”再套 SizeBox
		// 会把内部控件退化为默认 100x100 期望尺寸，导致大面积错位/压扁。
		bool bNeedsSizeBox = false;
		if (Node->Layout == EDesignLayout::None)
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
	{
		FLayoutMapper::ApplyGap(Widget.Children, Node->Gap, Node->Layout);
		if (Node->Layout == EDesignLayout::Wrap)
		{
			Widget.Properties.Add(TEXT("InnerSlotPadding"),
				FString::Printf(TEXT("%.1f,%.1f"), Node->Gap, Node->Gap));
		}
		else if (Node->Layout == EDesignLayout::Grid)
		{
			Widget.Properties.Add(TEXT("SlotPadding"),
				FString::Printf(TEXT("%.1f,%.1f,%.1f,%.1f"), Node->Gap, Node->Gap, Node->Gap, Node->Gap));
		}
	}

	if (Node->JustifyContent != EDesignJustifyContent::Start)
	{
		// Determine the container's main-axis size mode for Spacer suppression.
		// ParentLayout tells us if this container sits inside a CanvasPanel.
		const EDesignSizeMode MainAxisMode = (Node->Layout == EDesignLayout::Horizontal)
			? Node->Width.Mode : Node->Height.Mode;
		FLayoutMapper::ApplyJustifyContent(Widget.Children, Node->JustifyContent, MainAxisMode, ParentLayout);
	}

	TSharedPtr<FDesignNode> VisualNode = Node;
	const auto IsHexColorLiteral = [](const FString& ColorValue) -> bool
	{
		if (!ColorValue.StartsWith(TEXT("#")))
		{
			return false;
		}
		const int32 Len = ColorValue.Len();
		return Len == 4 || Len == 7 || Len == 9;
	};

	const bool bIsLeafContainer =
		(Node->Type == EDesignNodeType::Frame
			|| Node->Type == EDesignNodeType::Group
			|| Node->Type == EDesignNodeType::Rectangle) &&
		Node->Children.Num() == 0;
	const bool bIsLargeEnoughPlaceholderSurface = Node->Width.Value >= 24.0f && Node->Height.Value >= 12.0f;
	const bool bUseSlotPlaceholderTint =
		bIsLeafContainer
		&& bIsLargeEnoughPlaceholderSurface
		&& Node->bSemanticPlaceholderSurface;
	if (bUseSlotPlaceholderTint)
	{
		const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		const FString FillColor = IsHexColorLiteral(VisualNode->Fill.SolidColor) ? VisualNode->Fill.SolidColor : FString();
		const FString StrokeColor = IsHexColorLiteral(VisualNode->Stroke.Fill.SolidColor) ? VisualNode->Stroke.Fill.SolidColor : FString();
		const FString FallbackBgColor =
			(Settings && IsHexColorLiteral(Settings->PlaceholderHatchBackgroundColor))
			? Settings->PlaceholderHatchBackgroundColor
			: FString();
		const FString FallbackStripeColor =
			(Settings && IsHexColorLiteral(Settings->PlaceholderHatchStripeColor))
			? Settings->PlaceholderHatchStripeColor
			: FString();
		const int32 StripeWidth = (Settings && Settings->PlaceholderHatchStripeWidth > 0)
			? Settings->PlaceholderHatchStripeWidth
			: 1;
		const int32 StripeGap = (Settings && Settings->PlaceholderHatchStripeGap > 0)
			? Settings->PlaceholderHatchStripeGap
			: 4;
		const FString HatchBgColor = !FillColor.IsEmpty() ? FillColor : FallbackBgColor;
		const FString HatchStripeColor = !StrokeColor.IsEmpty() ? StrokeColor : FallbackStripeColor;
		if (!HatchBgColor.IsEmpty() && !HatchStripeColor.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: applying data-driven placeholder hatch to '%s'"), *Node->Name);
			Widget.Properties.Add(TEXT("MaterialRef"), TEXT("placeholderhatch"));
			Widget.Properties.Add(TEXT("MaterialParams"),
				FString::Printf(TEXT("{\"Color\":\"%s\",\"StripeColor\":\"%s\",\"StripeWidth\":%d,\"StripeGap\":%d}"),
					*HatchBgColor, *HatchStripeColor, StripeWidth, StripeGap));
			// 由程序化纹理负责颜色输出，避免 BrushColor 叠乘导致偏色。
			Widget.Properties.Remove(TEXT("BrushColor"));
			Widget.Brush.bHasBrushColor = false;
		}
	}

	const bool bHasVisualFill = (VisualNode->Fill.bEnabled && VisualNode->Fill.Type != EDesignFillType::None)
		|| VisualNode->Fills.Num() > 0;
	const bool bHasCornerRadius = VisualNode->CornerRadius.bUniform ? VisualNode->CornerRadius.Uniform > 0
		: (VisualNode->CornerRadius.TopLeft > 0 || VisualNode->CornerRadius.TopRight > 0
			|| VisualNode->CornerRadius.BottomRight > 0 || VisualNode->CornerRadius.BottomLeft > 0);
	const bool bTreatPaddingAsBorder = (Node->Layout == EDesignLayout::None);
	bool bNeedsBorder = (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
		&& (bHasVisualFill || (bTreatPaddingAsBorder && FLayoutMapper::HasPadding(VisualNode))
			|| bHasCornerRadius || VisualNode->Stroke.HasStroke());

	if (bNeedsBorder)
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
			*Widget.Name, bHasVisualFill, bHasCornerRadius, VisualNode->Stroke.HasStroke());
		Widget = FVisualMapper::WrapWithBorder(MoveTemp(Widget), VisualNode);
	}
	else if (FLayoutMapper::HasPadding(VisualNode))
		FLayoutMapper::ApplyPadding(Widget, VisualNode->Padding);

	// 多层填充叠加（Overlay 包裹）
	if (VisualNode->HasMultiFill())
		Widget = FVisualMapper::WrapWithMultiFill(MoveTemp(Widget), VisualNode);

	const FString* ShadowCountStr = Widget.Properties.Find(TEXT("BoxShadowCount"));
	if (ShadowCountStr)
	{
		int32 ShadowCount = FCString::Atoi(**ShadowCountStr);
		Widget.Properties.Remove(TEXT("BoxShadowCount"));
		for (int32 i = 0; i < ShadowCount; ++i)
			Widget = FVisualMapper::WrapWithBoxShadow(MoveTemp(Widget), VisualNode, i);
	}

	for (const FDesignEffect& Effect : VisualNode->Effects)
	{
		if (Effect.bEnabled && Effect.Type == EDesignEffectType::BackgroundBlur && Effect.Radius > 0)
			return FVisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), Effect.Radius);
	}

	return Widget;
}

FUMGWidgetNode FDesign2UMGConverter::CreateContainerWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
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
	{
		FString Typeface = FFontResolver::FindBestTypeface(Node->FontFamily, Node->FontWeight);
		if (Typeface.IsEmpty())
			Typeface = FDesignTypeMapper::MapFontWeight(Node->FontWeight).ToString();
		Widget.Properties.Add(TEXT("TypefaceFontName"), Typeface);
	}

	if (!Node->FontStyle.IsEmpty())
	{
		Widget.Properties.Add(TEXT("FontStyle"), Node->FontStyle);
	}

	Widget.Properties.Add(TEXT("Justification"),
		Node->TextAlign == EDesignTextAlign::Center ? TEXT("Center") :
		Node->TextAlign == EDesignTextAlign::Right ? TEXT("Right") : TEXT("Left"));

	if (Node->TextAlign == EDesignTextAlign::Justify)
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));

	// TextAlignVertical → parent Slot VerticalAlignment
	if (Node->TextAlignVertical == EDesignTextAlignVertical::Middle)
		Widget.Slot.VerticalAlignment = TEXT("Center");
	else if (Node->TextAlignVertical == EDesignTextAlignVertical::Bottom)
		Widget.Slot.VerticalAlignment = TEXT("Bottom");

	if (Node->LineHeight > 0)
		Widget.Properties.Add(TEXT("LineHeightPercentage"), FString::SanitizeFloat(Node->LineHeight));

	if (Node->LetterSpacing != 0)
	{
		Widget.Properties.Add(TEXT("LetterSpacing"), FString::SanitizeFloat(Node->LetterSpacing));
		Widget.TextStyle.LetterSpacing = Node->LetterSpacing;
		Widget.TextStyle.bHasLetterSpacing = true;
	}

	if (Node->bUnderline)
	{
		Widget.Properties.Add(TEXT("Underline"), TEXT("true"));
	}
	if (Node->bStrikethrough)
	{
		Widget.Properties.Add(TEXT("Strikethrough"), TEXT("true"));
	}
	if (!Node->Href.IsEmpty())
	{
		Widget.Properties.Add(TEXT("Href"), Node->Href);
	}

	switch (Node->TextGrowth)
	{
	case EDesignTextGrowth::FixedWidth:
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		break;
	case EDesignTextGrowth::FixedWidthHeight:
		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));
		break;
	default:
		break;
	}

	if (Node->TextAlignVertical != EDesignTextAlignVertical::Top)
	{
		Widget.Properties.Add(TEXT("TextAlignVertical"),
			Node->TextAlignVertical == EDesignTextAlignVertical::Middle ? TEXT("Center") : TEXT("Bottom"));
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
	bool bUsedResolverPath = false;
	if (Settings && !Node->IconFontFamily.IsEmpty())
	{
		AssetPath = Settings->FindIconFontAssetPath(Node->IconFontFamily);
		if (AssetPath.IsEmpty())
		{
			AssetPath = Settings->DefaultIconFontAssetPath;
			bUsedDefaultIconPath = !AssetPath.IsEmpty();
		}
	}

	if (!AssetPath.IsEmpty())
	{
		FString ValidAssetPath;
		if (TryNormalizeValidFontAssetPath(AssetPath, ValidAssetPath))
		{
			AssetPath = MoveTemp(ValidAssetPath);
		}
		else
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Icon font path is stale or invalid for family '%s': %s"),
				*Node->IconFontFamily,
				*AssetPath);
			if (MutableSettings && !Node->IconFontFamily.IsEmpty())
			{
				static TSet<FString> PrunedInvalidMappings;
				if (!PrunedInvalidMappings.Contains(Node->IconFontFamily)
					&& MutableSettings->IconFontAssetMap.Remove(Node->IconFontFamily) > 0)
				{
					PrunedInvalidMappings.Add(Node->IconFontFamily);
					MutableSettings->SaveConfig();
				}
			}
			AssetPath.Empty();
			bUsedDefaultIconPath = false;
		}
	}

	// Fallback: resolve icon family through general font resolver.
	// This avoids "Last resort fallback font" when icon font has been auto-imported
	// but IconFontAssetMap is not explicitly configured.
	if (AssetPath.IsEmpty() && !Node->IconFontFamily.IsEmpty())
	{
		FString ResolvedAssetPath = FFontResolver::FindFontAssetPath(Node->IconFontFamily);
		FString ValidResolvedPath;
		if (TryNormalizeValidFontAssetPath(ResolvedAssetPath, ValidResolvedPath))
		{
			AssetPath = MoveTemp(ValidResolvedPath);
			bUsedResolverPath = true;
		}
	}

	// If family mapping is missing but we found a usable path, persist mapping once.
	const bool bNeedsPersistMapping = (bUsedDefaultIconPath || bUsedResolverPath)
		&& MutableSettings
		&& !Node->IconFontFamily.IsEmpty();
	if (bNeedsPersistMapping)
	{
		const TSoftObjectPtr<UFont>* Existing = MutableSettings->IconFontAssetMap.Find(Node->IconFontFamily);
		if (!Existing || Existing->IsNull())
		{
			MutableSettings->IconFontAssetMap.Add(Node->IconFontFamily, TSoftObjectPtr<UFont>(FSoftObjectPath(AssetPath)));
			UE_LOG(LogDesign2UMG, Display,
				TEXT("Auto-mapped icon font family '%s' to '%s'%s"),
				*Node->IconFontFamily,
				*AssetPath,
				bUsedResolverPath ? TEXT(" via FontResolver") : TEXT(""));
			MutableSettings->SaveConfig();
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
				TEXT("图标字体 '%s' (字体族 '%s') 未配置资产映射。请在 编辑器偏好设置 > 插件 > Design2UMG 中配置。"),
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

	if (Node->Fill.Type == EDesignFillType::Color)
		Widget.Properties.Add(TEXT("Color"), Node->Fill.SolidColor);

	return Widget;
}

FUMGWidgetNode FDesign2UMGConverter::CreatePathWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;

	// 已被 FigmaImageExporter 转为 Image Fill → 走 Image 路径
	if (Node->Fill.Type == EDesignFillType::Image && !Node->Fill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
		if (Node->Fill.Image.Mode == EDesignImageMode::Crop && !Node->Fill.Image.Transform.IsIdentity())
		{
			const FDesignImageTransform& T = Node->Fill.Image.Transform;
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Crop"));
			Widget.Properties.Add(TEXT("ImageTransform"),
				FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
					T.A, T.B, T.TX, T.C, T.D, T.TY));
		}
		else if (Node->Fill.Image.Mode == EDesignImageMode::Fill)
		{
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fill"));
		}
		else if (Node->Fill.Image.Mode == EDesignImageMode::Fit)
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
		if (Node->Fill.Type == EDesignFillType::Color)
			RasterColor = Node->Fill.SolidColor;
		else if (Node->Fill.Type == EDesignFillType::Gradient && Node->Fill.Gradient.Colors.Num() > 0)
			RasterColor = Node->Fill.Gradient.Colors[0].Color;
		else if (Node->Stroke.HasStroke() && Node->Stroke.Fill.Type == EDesignFillType::Color)
			RasterColor = Node->Stroke.Fill.SolidColor;
		else
		{
			for (const FDesignFill& Fill : Node->Fills)
			{
				if (Fill.Type == EDesignFillType::Color)
				{
					RasterColor = Fill.SolidColor;
					break;
				}
				if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
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
	Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
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
	if (Node->Type == EDesignNodeType::Ref)
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
	if (Node->Type == EDesignNodeType::Text)
	{
		float W = Node->Width.Value;
		float H = Node->Height.Value;
		const bool bNeedWidthEstimate = !Node->Width.IsFixed() && W <= 0.0f;
		const bool bNeedHeightEstimate = !Node->Height.IsFixed() && H <= 0.0f;
		if ((bNeedWidthEstimate || bNeedHeightEstimate) && Node->FontSize > 0 && !Node->Content.IsEmpty())
		{
			FVector2D Measured = MeasureTextSize(Node->Content, Node->FontSize, Node->FontFamily, Node->FontWeight);
			if (bNeedWidthEstimate) W = Measured.X;
			if (bNeedHeightEstimate) H = Measured.Y;
		}
		else
		{
			if (bNeedHeightEstimate && Node->FontSize > 0)
				H = Node->FontSize * 1.5f;
		}
		// 仅在 FitContent 且无像素值时回写，避免污染 FillContainer 语义。
		if (W > 0 && Node->Width.IsFit() && Node->Width.Value <= 0.0f)
			Node->Width.Value = W;
		if (H > 0 && Node->Height.IsFit() && Node->Height.Value <= 0.0f)
			Node->Height.Value = H;
		return FVector2D(W, H);
	}

	// IconFont 叶子节点
	if (Node->Type == EDesignNodeType::IconFont)
	{
		const float S = Node->FontSize > 0 ? Node->FontSize : 16.0f;
		float W = Node->Width.Value;
		float H = Node->Height.Value;
		if (!Node->Width.IsFixed() && W <= 0.0f)
			W = S;
		if (!Node->Height.IsFixed() && H <= 0.0f)
			H = S;

		// 仅回写 FitContent，避免 FillContainer 被误改为像素 fallback。
		if (W > 0 && Node->Width.IsFit() && Node->Width.Value <= 0.0f)
			Node->Width.Value = W;
		if (H > 0 && Node->Height.IsFit() && Node->Height.Value <= 0.0f)
			Node->Height.Value = H;
		return FVector2D(W, H);
	}

	// 矢量叶子节点（Path/Line/Ellipse/Polygon）：亚像素钳制到 1px 并回写
	// CreatePathWidget 用 Max(1, RoundToInt) 算 WidthOverride，这里对齐
	const bool bIsVectorLeaf = Node->Type == EDesignNodeType::Path
		|| Node->Type == EDesignNodeType::Line
		|| Node->Type == EDesignNodeType::Ellipse
		|| Node->Type == EDesignNodeType::Polygon;
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
	if (!Node->IsContainer() && Node->Type != EDesignNodeType::Rectangle)
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



