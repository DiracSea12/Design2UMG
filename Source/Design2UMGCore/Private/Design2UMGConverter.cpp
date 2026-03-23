// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverter.h"
#include "DesignTypeMapper.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "FontResolver.h"
#include "ImageFillUtils.h"
#include "LayoutMapper.h"
#include "PathWidgetBuilder.h"
#include "SizeEstimator.h"
#include "TextMeasurement.h"
#include "VisualMapper.h"
#include "RefExpander.h"
#include "Misc/PackageName.h"

namespace
{
	constexpr int32 MaxTreeRecursionDepth = 512;

	struct FConversionState
	{
		explicit FConversionState(const FDesignDocument& InDocument, TArray<FString>& InWarnings)
			: Document(InDocument)
			, Warnings(InWarnings)
		{
		}

		const FDesignDocument& Document;
		TArray<FString>& Warnings;
		TSet<FString> WarnedFamilies;
		TSet<FString> PrunedInvalidMappings;
		TMap<FString, FString> PendingIconFontMappings;
		int32 RecursionDepth = 0;
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

	bool HasVisualFill(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() && (HasVisibleFill(Node->Fill) || Node->Fills.Num() > 0);
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

	TSharedPtr<FDesignNode> ExpandRefForConversion(
		const TSharedPtr<FDesignNode>& Node,
		const FConversionContext& Context)
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

	bool HasNodeMetadataFlag(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key)
	{
		const FString* Value = Node->Metadata.Find(Key);
		return Value && Value->Equals(TEXT("true"), ESearchCase::IgnoreCase);
	}

	bool HasWidgetStringProperty(const FUMGWidgetNode& Widget, const TCHAR* Key)
	{
		FString Value;
		return Widget.Properties.TryGetString(Key, Value) && !Value.IsEmpty();
	}

	bool TryApplyPlaceholderSurfaceTint(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		const bool bExpandedRefInstancePlaceholder = HasNodeMetadataFlag(Node, TEXT("design2umg.expanded_ref_instance"));
		const FString DefaultPlaceholderFillColor = bExpandedRefInstancePlaceholder ? TEXT("#9682FF33") : TEXT("#D282FF33");
		const FString DefaultPlaceholderOutlineColor = bExpandedRefInstancePlaceholder ? TEXT("#9580FF") : TEXT("#D480FF");
		const bool bIsLeafContainer =
			(Node->Type == EDesignNodeType::Frame
				|| Node->Type == EDesignNodeType::Group
				|| Node->Type == EDesignNodeType::Rectangle)
			&& Node->Children.Num() == 0;
		const bool bIsExplicitEmptySlotSurface =
			HasNodeMetadataFlag(Node, TEXT("pen.slot.empty")) || HasNodeMetadataFlag(Node, TEXT("figma.slot.empty"));
		const bool bUseSlotPlaceholderTint =
			bIsLeafContainer
			&& Node->Width.Value >= 24.0f
			&& Node->Height.Value >= 12.0f
			&& (Node->bSemanticPlaceholderSurface || bIsExplicitEmptySlotSurface);
		if (!bUseSlotPlaceholderTint)
			return false;

		const FString FillColor = IsHexColorLiteral(Node->Fill.SolidColor) ? Node->Fill.SolidColor : FString();
		const FString StrokeColor = IsHexColorLiteral(Node->Stroke.Fill.SolidColor) ? Node->Stroke.Fill.SolidColor : FString();
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
			FMath::RoundToInt(FMath::Min3(10.0f, Node->Width.Value * 0.25f, Node->Height.Value * 0.25f)),
			1,
			10);
		Widget.Brush = FUMGVisualBrushData();
		for (const TCHAR* Key : {TEXT("BrushColor"), TEXT("TexturePath"), TEXT("DrawAs"), TEXT("OutlineThickness"), TEXT("OutlineColor"), TEXT("StrokePadding"), TEXT("StrokeMargin")})
			Widget.Properties.Remove(Key);
		Widget.Properties.Add(TEXT("MaterialRef"), TEXT("placeholdersurface"));
		Widget.Properties.Add(TEXT("MaterialParams"),
			FString::Printf(TEXT("{\"Color\":\"%s\",\"StripeColor\":\"%s\",\"StripeWidth\":1,\"Inset\":%d}"),
				*PlaceholderFillColor, *PlaceholderOutlineColor, InsetPx));
		if (Node->Width.Value > 0.0f)
			Widget.Properties.Add(TEXT("WidthOverride"), FString::SanitizeFloat(Node->Width.Value));
		if (Node->Height.Value > 0.0f)
			Widget.Properties.Add(TEXT("HeightOverride"), FString::SanitizeFloat(Node->Height.Value));
		return true;
	}

	bool ShouldWrapNodeWithBorder(const TSharedPtr<FDesignNode>& Node, bool bPlaceholderUsesDirectImage)
	{
		return (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
			&& (HasVisualFill(Node)
				|| (((Node->Layout == EDesignLayout::None)
					|| (Node->Layout == EDesignLayout::Wrap)
					|| (Node->Layout == EDesignLayout::Grid))
					&& FLayoutMapper::HasPadding(Node))
				|| Node->CornerRadius.HasRadius()
				|| Node->Stroke.HasStroke())
			&& !bPlaceholderUsesDirectImage;
	}

	FUMGWidgetNode ApplyVisualEffectWrappers(FUMGWidgetNode Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->HasMultiFill())
			Widget = FVisualMapper::WrapWithMultiFill(MoveTemp(Widget), Node);

		int32 ShadowCount = 0;
		if (Widget.Properties.TryGetInt(TEXT("BoxShadowCount"), ShadowCount) && ShadowCount > 0)
		{
			Widget.Properties.Remove(TEXT("BoxShadowCount"));
			for (int32 Index = 0; Index < ShadowCount; ++Index)
				Widget = FVisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, Index);
		}

		for (const FDesignEffect& Effect : Node->Effects)
		{
			if (Effect.bEnabled && Effect.Type == EDesignEffectType::BackgroundBlur && Effect.Radius > 0)
				return FVisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), Effect.Radius);
		}

		return Widget;
	}

}

FUMGWidgetNode ConvertNode(const TSharedPtr<FDesignNode>& Node, const FConversionContext& Context, EDesignLayout ParentLayout = EDesignLayout::None);
FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FDesignNode>& Node);
FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node);
FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node, FConversionState& State);
void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
TSharedPtr<FDesignNode> ResolveExpandedChildForConversion(const TSharedPtr<FDesignNode>& Child, const FConversionContext& Context, bool bPreComputeFitContent);
void PreResolveFlowChildren(const TSharedPtr<FDesignNode>& Node, const FUMGWidgetNode& Widget, const TArray<TSharedPtr<FDesignNode>>& VisibleChildren, const FConversionContext& Context);
bool ShouldWrapChildWithSizeBox(const TSharedPtr<FDesignNode>& ParentNode, const TSharedPtr<FDesignNode>& ChildNode, const FUMGWidgetNode& ChildWidget);
void AppendConvertedChildren(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node, const TArray<TSharedPtr<FDesignNode>>& VisibleChildren, const FConversionContext& Context);

FUMGWidgetData FDesign2UMGConverter::Convert(const FDesignDocument& Document)
{
	FUMGWidgetData Result;
	FConversionState State(Document, Result.Warnings);
	FConversionContext RootContext(State);

	TextMeasurement::ResetCache();

	// 深拷贝 Children，避免通过 TSharedPtr 修改调用者的原始数据
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

	// WidgetName 使用第一个非跳过 frame 的名称，fallback 到 WBP_FromPen
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

	// 预计算 FitContent 节点的实际尺寸（从子节点递归推算）
	for (const auto& Child : VisibleChildren)
	{
		SizeEstimator::PreComputeFitContentSizes(Child, Document);
	}

	TArray<FUMGWidgetNode> RenderWidgets;
	for (const auto& Child : VisibleChildren)
	{
		if (Child->bReusable)
			continue;

		Child->X = 0.0f;
		Child->Y = 0.0f;

		FUMGWidgetNode ChildWidget = ConvertNode(Child, RootContext);
		if (!ChildWidget.Type.IsEmpty())
			RenderWidgets.Add(MoveTemp(ChildWidget));
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

FUMGWidgetNode ConvertNode(const TSharedPtr<FDesignNode>& Node, const FConversionContext& Context, EDesignLayout ParentLayout)
{
	FRecursionGuard Guard(Context.State.RecursionDepth, TEXT("ConvertNode"));
	if (!Guard.bValid)
		return FUMGWidgetNode();

	if (ShouldSkipNode(Node))
		return FUMGWidgetNode();

	const FConversionContext ActiveContext = Context.WithNodeTheme(Node);

	if (Node->Type == EDesignNodeType::Ref)
	{
		auto Expanded = ExpandRefForConversion(Node, ActiveContext);
		if (!Expanded.IsValid())
			return FUMGWidgetNode();
		SizeEstimator::PreComputeFitContentSizes(Expanded, Context.State.Document);
		return ConvertNode(Expanded, ActiveContext, ParentLayout);
	}

	FUMGWidgetNode Widget;

	switch (Node->Type)
	{
	case EDesignNodeType::Text:
		Widget = CreateTextWidget(Node);
		break;
	case EDesignNodeType::IconFont:
		Widget = CreateIconFontWidget(Node, Context.State);
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
			bool bHasGradientFill = Node->Fill.Type == EDesignFillType::Gradient;
			if (!bHasGradientFill)
			{
				for (const FDesignFill& Fill : Node->Fills)
				{
					if (Fill.Type == EDesignFillType::Gradient)
					{
						bHasGradientFill = true;
						break;
					}
				}
			}
			const bool bHasVectorRasterFallback = bHasGradientFill && Node->Metadata.Contains(TEXT("figma.vectorRasterFallback"));
			// 有尺寸、有颜色填充、无描边 → 生成椭圆 SVG path，走栅格化
			// 有描边时走 fallback（Border 支持 OutlineThickness，Image 不支持）
			bool bHasStroke = Node->Stroke.HasStroke();
			if (W > 0 && H > 0 && ((Node->Fill.IsColor() && Node->Fill.bEnabled && !bHasStroke) || bHasVectorRasterFallback))
			{
				float Rx = W / 2.0f, Ry = H / 2.0f;
				Node->Geometry = FString::Printf(
					TEXT("M 0,%.2f A %.2f,%.2f 0 1,1 %.2f,%.2f A %.2f,%.2f 0 1,1 0,%.2f Z"),
					Ry, Rx, Ry, W, Ry, Rx, Ry, Ry);
				Widget = PathWidgetBuilder::CreatePathWidget(Node);
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
			Widget = PathWidgetBuilder::CreatePathWidget(Node);
		}
		else
		{
			Widget = CreateContainerWidget(Node);
			UE_LOG(LogDesign2UMG, Warning, TEXT("多边形 '%s' 无 PathGeometry，降级为 Border"), *Node->Name);
			Context.State.Warnings.Add(FString::Printf(TEXT("Polygon '%s' → Border (无 PathGeometry)"), *Node->Name));
		}
		break;
	case EDesignNodeType::Path:
		Widget = PathWidgetBuilder::CreatePathWidget(Node);
		break;
	case EDesignNodeType::Line:
		if (!Node->Geometry.IsEmpty())
		{
			Widget = PathWidgetBuilder::CreatePathWidget(Node);
		}
		else
		{
			// 无 Geometry 时用 Width/Height 构造水平线段
			float W = FMath::Max(1.0f, Node->Width.Value);
			float H = FMath::Max(1.0f, Node->Height.Value);
			Node->Geometry = FString::Printf(TEXT("M 0,%.1f L %.1f,%.1f"), H * 0.5f, W, H * 0.5f);
			Widget = PathWidgetBuilder::CreatePathWidget(Node);
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
		&& !HasWidgetStringProperty(Widget, TEXT("BrushColor"))
		&& !HasWidgetStringProperty(Widget, TEXT("TexturePath"))
		&& !HasWidgetStringProperty(Widget, TEXT("MaterialRef")))
	{
		// Stroke-only / no-fill Border should keep transparent interior.
		// UBorder default brush is opaque white, which can cover siblings.
		Widget.Properties.Add(TEXT("BrushColor"), TEXT("#00000000"));
		if (HasWidgetStringProperty(Widget, TEXT("OutlineThickness")))
		{
			// Box + WhiteSquareTexture can still flash an opaque interior in UBorder.
			// RoundedBox with zero radii keeps the interior transparent while preserving the outline.
			Widget.Properties.Add(TEXT("DrawAs"), TEXT("RoundedBox"));
		}
	}

	TArray<TSharedPtr<FDesignNode>> VisibleChildren;
	SizeEstimator::GatherVisibleChildren(Node, VisibleChildren);
	PreResolveFlowChildren(Node, Widget, VisibleChildren, ActiveContext);
	AppendConvertedChildren(Widget, Node, VisibleChildren, ActiveContext);

	if (Node->Gap > 0)
	{
		if (Node->Layout == EDesignLayout::Wrap)
		{
			const float CrossAxisGap = Node->FigmaLayout.bHasCounterAxisSpacing
				? Node->FigmaLayout.CounterAxisSpacing
				: Node->Gap;
			Widget.Properties.Add(TEXT("InnerSlotPadding"),
				FString::Printf(TEXT("%.1f,%.1f"), Node->Gap, CrossAxisGap));
		}
		else
		{
			FLayoutMapper::ApplyGap(Widget.Children, Node->Gap, Node->Layout);
		}

		if (Node->Layout == EDesignLayout::Grid)
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

	const bool bPlaceholderUsesDirectImage = TryApplyPlaceholderSurfaceTint(Widget, Node);
	if (ShouldWrapNodeWithBorder(Node, bPlaceholderUsesDirectImage))
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: '%s' → WrapWithBorder (fill=%d corner=%d stroke=%d)"),
			*Widget.Name, HasVisualFill(Node), Node->CornerRadius.HasRadius(), Node->Stroke.HasStroke());
		Widget = FVisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	}
	else if (FLayoutMapper::HasPadding(Node) && !bPlaceholderUsesDirectImage)
	{
		FLayoutMapper::ApplyPadding(Widget, Node->Padding);
	}

	return ApplyVisualEffectWrappers(MoveTemp(Widget), Node);
}

FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}

FUMGWidgetNode CreateTextWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");
	Widget.Properties.Add(TEXT("Text"), Node->Content);
	ApplyTextProperties(Widget, Node);
	return Widget;
}

void ApplyTextProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	const float ResolvedFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
	if (ResolvedFontSize > 0)
	{
		Widget.Properties.Add(TEXT("FontSize"), FString::SanitizeFloat(ResolvedFontSize));
		Widget.Properties.Add(TEXT("DesignFontSize"), FString::SanitizeFloat(Node->FontSize));
		Widget.TextStyle.FontSize = ResolvedFontSize;
		Widget.TextStyle.SourceFontSize = Node->FontSize;
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

	// textAlignVertical is alignment inside the text box, not the parent layout slot.
	// Mapping it onto the parent slot shifts text widgets inside buttons/tabs/tables and
	// causes widespread component drift. Keep it as text metadata only.

	const bool bApplyRuntimeLineHeight =
		TextMeasurement::ShouldApplyExplicitLineHeight(Node, ResolvedFontSize)
		|| (Node->LineHeight > 0.0f
			&& Node->FontSize >= 24.0f
			&& !Node->Content.Contains(TEXT("\n"))
			&& !TextMeasurement::IsWrappingTextMode(Node));
	if (bApplyRuntimeLineHeight)
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
	case EDesignTextGrowth::FixedWidthHeight:
		if (HasNodeMetadataFlag(Node, TEXT("figma.text.truncate")))
		{
			Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));
			break;
		}

		Widget.Properties.Add(TEXT("AutoWrapText"), TEXT("true"));
		if (const float WrapTextAt = TextMeasurement::ResolveTextWrapAt(Node, 0.0f); WrapTextAt > 0.0f)
		{
			Widget.Properties.Add(TEXT("WrapTextAt"), FString::SanitizeFloat(WrapTextAt));
		}
		if (Node->TextGrowth == EDesignTextGrowth::FixedWidthHeight)
		{
			Widget.Properties.Add(TEXT("Clipping"), TEXT("ClipToBounds"));
		}
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

FUMGWidgetNode CreateIconFontWidget(const TSharedPtr<FDesignNode>& Node, FConversionState& State)
{
	FUMGWidgetNode Widget;
	Widget.Type = TEXT("TextBlock");

	const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
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
			if (!Node->IconFontFamily.IsEmpty())
			{
				State.PrunedInvalidMappings.Add(Node->IconFontFamily);
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
		&& !Node->IconFontFamily.IsEmpty();
	if (bNeedsPersistMapping)
	{
		State.PendingIconFontMappings.Add(Node->IconFontFamily, AssetPath);
		UE_LOG(LogDesign2UMG, Display,
			TEXT("Queued icon font family mapping '%s' -> '%s'%s"),
			*Node->IconFontFamily,
			*AssetPath,
			bUsedResolverPath ? TEXT(" via FontResolver") : TEXT(""));
	}

	if (!AssetPath.IsEmpty())
	{
		Widget.Properties.Add(TEXT("FontFamily"), AssetPath);
		const FString Typeface = !Node->FontWeight.IsEmpty()
			? FFontResolver::FindBestTypeface(Node->IconFontFamily, Node->FontWeight)
			: TEXT("Regular");
		if (!Typeface.IsEmpty())
		{
			Widget.Properties.Add(TEXT("TypefaceFontName"), Typeface);
		}
	}
	else if (!Node->IconFontFamily.IsEmpty())
	{
		if (!State.WarnedFamilies.Contains(Node->IconFontFamily))
		{
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("IconFont '%s': font family '%s' has no asset mapping"),
				*Node->IconFontName, *Node->IconFontFamily);
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Icon font '%s' (family '%s') has no configured asset mapping. Configure it in Editor Preferences > Plugins > Design2UMG."),
				*Node->IconFontName, *Node->IconFontFamily);
			State.WarnedFamilies.Add(Node->IconFontFamily);
		}
	}

	// 图标名称即连字文本（如 "dashboard" 在 Material Symbols 中渲染为图标）
	Widget.Properties.Add(TEXT("IconName"), Node->IconFontName);
	FString IconText = FFontResolver::FindKnownIconGlyph(Node->IconFontFamily, Node->IconFontName);
	if (IconText.IsEmpty())
	{
		IconText = Node->IconFontName;
	}
	if (AssetPath.IsEmpty() && Settings && !Settings->MissingIconFontPlaceholder.IsEmpty())
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

TSharedPtr<FDesignNode> ResolveExpandedChildForConversion(
	const TSharedPtr<FDesignNode>& Child,
	const FConversionContext& Context,
	bool bPreComputeFitContent)
{
	TSharedPtr<FDesignNode> ExpandedChild = ExpandRefForConversion(Child, Context);
	if (!ExpandedChild.IsValid())
		return Child;
	if (bPreComputeFitContent && ExpandedChild != Child)
		SizeEstimator::PreComputeFitContentSizes(ExpandedChild, Context.State.Document);
	return ExpandedChild;
}

void PreResolveFlowChildren(
	const TSharedPtr<FDesignNode>& Node,
	const FUMGWidgetNode& Widget,
	const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
	const FConversionContext& Context)
{
	if (Widget.Bounds.W > 0 && Widget.Bounds.H > 0
		&& (Node->Layout == EDesignLayout::Horizontal || Node->Layout == EDesignLayout::Vertical))
	{
		const float InnerW = Widget.Bounds.W - Node->Padding.Left - Node->Padding.Right;
		const float InnerH = Widget.Bounds.H - Node->Padding.Top - Node->Padding.Bottom;
		const bool bHorizontal = Node->Layout == EDesignLayout::Horizontal;
		TArray<FChildSizeSnapshot> EffectiveChildren;
		for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
			EffectiveChildren.Add(BuildChildSizeSnapshot(Child, Context.State.Document));

		float FixedMainSum = 0.0f;
		int32 FillCount = 0;
		const float TotalGap = EffectiveChildren.Num() > 1 ? Node->Gap * (EffectiveChildren.Num() - 1) : 0.0f;
		for (const FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
		{
			const FDesignSize& MainSize = bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
			if (MainSize.IsFill())
				++FillCount;
			else
				FixedMainSum += MainSize.Value;

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

		if (FillCount > 0)
		{
			const float AvailableMain = (bHorizontal ? InnerW : InnerH) - FixedMainSum - TotalGap;
			const float FillMain = FMath::Max(0.0f, AvailableMain / FillCount);
			for (FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
			{
				const FDesignSize& MainSize = bHorizontal ? ChildSnapshot.EffectiveWidth : ChildSnapshot.EffectiveHeight;
				if (MainSize.IsFill())
					SizeEstimator::SetResolvedNodeDimension(ChildSnapshot.Child, bHorizontal, FillMain);
				const FDesignSize& CrossSize = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
				if (CrossSize.IsFill())
					SizeEstimator::SetResolvedNodeDimension(ChildSnapshot.Child, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}

			UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFill: '%s' layout=%s inner=(%.0f,%.0f) fixed=%.0f fill=%d each=%.0f"),
				*Widget.Name, bHorizontal ? TEXT("H") : TEXT("V"),
				InnerW, InnerH, FixedMainSum, FillCount, FillMain);
		}
		else
		{
			for (FChildSizeSnapshot& ChildSnapshot : EffectiveChildren)
			{
				const FDesignSize& CrossSize = bHorizontal ? ChildSnapshot.EffectiveHeight : ChildSnapshot.EffectiveWidth;
				if (CrossSize.IsFill())
					SizeEstimator::SetResolvedNodeDimension(ChildSnapshot.Child, !bHorizontal, bHorizontal ? InnerH : InnerW);
			}
		}
	}

	for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
	{
		if (Child->Width.IsFit() || Child->Height.IsFit())
		{
			const float ChildContentWidth = SizeEstimator::ComputeChildContentWidthHint(Node, Child, 0.0f);
			const TSharedPtr<FDesignNode> EstimateChild = ResolveExpandedChildForConversion(Child, Context, false);
			const FVector2D Estimate = SizeEstimator::EstimateNodeSize(EstimateChild, ChildContentWidth);
			SizeEstimator::PromoteFitNodeDimension(Child, true, Estimate.X);
			SizeEstimator::PromoteFitNodeDimension(Child, false, Estimate.Y);
			if (Estimate.X > 0 || Estimate.Y > 0)
			{
				UE_LOG(LogDesign2UMG, Log, TEXT("PreResolveFitContent: '%s' estimated=(%.0f,%.0f)"),
					*Child->Name, Estimate.X, Estimate.Y);
			}
		}
	}
}

bool ShouldWrapChildWithSizeBox(
	const TSharedPtr<FDesignNode>& ParentNode,
	const TSharedPtr<FDesignNode>& ChildNode,
	const FUMGWidgetNode& ChildWidget)
{
	const bool bHasFixedSize = ChildNode->Width.IsFixed() || ChildNode->Height.IsFixed();
	const bool bHasMinSize = (ChildNode->Width.IsFit() && ChildNode->Width.Value > 0)
		|| (ChildNode->Height.IsFit() && ChildNode->Height.Value > 0);
	const bool bCanvasMixedAxis = ParentNode->Layout == EDesignLayout::None
		&& ((ChildNode->Width.IsFit() && ChildNode->Height.IsFixed())
			|| (ChildNode->Height.IsFit() && ChildNode->Width.IsFixed()));
	const bool bWrapOrGridMeasuredFill =
		(ParentNode->Layout == EDesignLayout::Wrap || ParentNode->Layout == EDesignLayout::Grid)
		&& ((ChildNode->Width.IsFill() && ChildNode->Width.Value > 0.0f)
			|| (ChildNode->Height.IsFill() && ChildNode->Height.Value > 0.0f));
	const bool bWidgetOwnsDesiredSize =
		ParentNode->Layout != EDesignLayout::None
		&& ChildWidget.Type == TEXT("Image")
		&& ChildNode->Children.Num() == 0
		&& (HasWidgetStringProperty(ChildWidget, TEXT("WidthOverride")) || HasWidgetStringProperty(ChildWidget, TEXT("HeightOverride")))
		&& (HasWidgetStringProperty(ChildWidget, TEXT("MaterialRef"))
			|| HasWidgetStringProperty(ChildWidget, TEXT("TexturePath"))
			|| HasWidgetStringProperty(ChildWidget, TEXT("PathGeometry")));
	return ParentNode->Layout == EDesignLayout::None
		? bCanvasMixedAxis
		: (bHasFixedSize || bHasMinSize || bWrapOrGridMeasuredFill) && !bWidgetOwnsDesiredSize;
}

void AppendConvertedChildren(
	FUMGWidgetNode& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const TArray<TSharedPtr<FDesignNode>>& VisibleChildren,
	const FConversionContext& Context)
{
	for (const TSharedPtr<FDesignNode>& Child : VisibleChildren)
	{
		TSharedPtr<FDesignNode> EffectiveChild = ResolveExpandedChildForConversion(Child, Context, true);
		FUMGWidgetNode ChildWidget = ConvertNode(EffectiveChild, Context, Node->Layout);
		if (ChildWidget.Type.IsEmpty())
			continue;

		FLayoutMapper::ApplySlotProperties(ChildWidget, EffectiveChild, Node->Layout);
		FLayoutMapper::ApplyAlignItems(ChildWidget, Node->AlignItems, Node->Layout);
		if (Node->Layout != EDesignLayout::None && !EffectiveChild->bAbsoluteLayoutPositioning)
		{
			ChildWidget.Bounds.X = 0.0f;
			ChildWidget.Bounds.Y = 0.0f;
		}

		if (ShouldWrapChildWithSizeBox(Node, EffectiveChild, ChildWidget))
		{
			const bool bHasFixedSize = EffectiveChild->Width.IsFixed() || EffectiveChild->Height.IsFixed();
			const bool bHasMinSize = (EffectiveChild->Width.IsFit() && EffectiveChild->Width.Value > 0)
				|| (EffectiveChild->Height.IsFit() && EffectiveChild->Height.Value > 0);
			UE_LOG(LogDesign2UMG, Verbose, TEXT("  child '%s' → SizeBox wrap (fixed=%d min=%d w=%.1f h=%.1f)"),
				*ChildWidget.Name, bHasFixedSize, bHasMinSize,
				EffectiveChild->Width.Value, EffectiveChild->Height.Value);
			Widget.Children.Add(FLayoutMapper::WrapWithSizeBox(MoveTemp(ChildWidget), EffectiveChild, Node->Layout));
			continue;
		}

		Widget.Children.Add(MoveTemp(ChildWidget));
	}
}

