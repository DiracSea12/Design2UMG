// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "FigmaParser.h"
#include "Design2UMGLog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Internationalization/Regex.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static FString GetStr(const TSharedPtr<FJsonObject>& Obj, const FString& Key, const FString& Default = FString())
{
	FString Val;
	return (Obj && Obj->TryGetStringField(Key, Val)) ? Val : Default;
}

static double GetNum(const TSharedPtr<FJsonObject>& Obj, const FString& Key, double Default = 0.0)
{
	double Val;
	return (Obj && Obj->TryGetNumberField(Key, Val)) ? Val : Default;
}

static bool GetBool(const TSharedPtr<FJsonObject>& Obj, const FString& Key, bool Default = true)
{
	bool Val;
	return (Obj && Obj->TryGetBoolField(Key, Val)) ? Val : Default;
}

static TSharedPtr<FJsonObject> GetObj(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
	const TSharedPtr<FJsonObject>* Sub = nullptr;
	if (Obj && Obj->TryGetObjectField(Key, Sub))
		return *Sub;
	return nullptr;
}

static const TArray<TSharedPtr<FJsonValue>>* GetArr(const TSharedPtr<FJsonObject>& Obj, const FString& Key)
{
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Obj && Obj->TryGetArrayField(Key, Arr))
		return Arr;
	return nullptr;
}

static TSharedPtr<FJsonObject> ParseJsonString(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	auto Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Root);
	return Root;
}

static bool IsVisibleSolidBlackFill(const TSharedPtr<FJsonObject>& NodeObj)
{
	const auto* Fills = GetArr(NodeObj, TEXT("fills"));
	if (!Fills || Fills->Num() == 0)
		return false;

	const TSharedPtr<FJsonObject> FillObj = (*Fills)[0]->AsObject();
	if (!FillObj.IsValid() || !GetBool(FillObj, TEXT("visible"), true))
		return false;
	if (GetStr(FillObj, TEXT("type")) != TEXT("SOLID"))
		return false;

	const TSharedPtr<FJsonObject> ColorObj = GetObj(FillObj, TEXT("color"));
	if (!ColorObj.IsValid())
		return false;

	const double R = GetNum(ColorObj, TEXT("r"), 1.0);
	const double G = GetNum(ColorObj, TEXT("g"), 1.0);
	const double B = GetNum(ColorObj, TEXT("b"), 1.0);
	const double A = GetNum(ColorObj, TEXT("a"), 1.0);
	return R <= 0.01 && G <= 0.01 && B <= 0.01 && A >= 0.99;
}

static bool HasSameBoundsAsParent(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj)
{
	const TSharedPtr<FJsonObject> ChildBBox = GetObj(NodeObj, TEXT("absoluteBoundingBox"));
	const TSharedPtr<FJsonObject> ParentBBox = GetObj(ParentObj, TEXT("absoluteBoundingBox"));
	if (!ChildBBox.IsValid() || !ParentBBox.IsValid())
		return false;

	const double CX = GetNum(ChildBBox, TEXT("x"));
	const double CY = GetNum(ChildBBox, TEXT("y"));
	const double CW = GetNum(ChildBBox, TEXT("width"));
	const double CH = GetNum(ChildBBox, TEXT("height"));
	const double PX = GetNum(ParentBBox, TEXT("x"));
	const double PY = GetNum(ParentBBox, TEXT("y"));
	const double PW = GetNum(ParentBBox, TEXT("width"));
	const double PH = GetNum(ParentBBox, TEXT("height"));
	return FMath::IsNearlyEqual(CX, PX, 0.5) &&
		FMath::IsNearlyEqual(CY, PY, 0.5) &&
		FMath::IsNearlyEqual(CW, PW, 0.5) &&
		FMath::IsNearlyEqual(CH, PH, 0.5);
}

static FString NormalizeWindingRule(const FString& InRule)
{
	if (InRule.Equals(TEXT("EVENODD"), ESearchCase::IgnoreCase))
		return TEXT("evenodd");
	return TEXT("nonzero");
}

static bool ParseGeometryArray(
	const TArray<TSharedPtr<FJsonValue>>* GeometryArr,
	FString& OutPath,
	FString& OutRule)
{
	if (!GeometryArr)
		return false;

	for (const TSharedPtr<FJsonValue>& GeometryVal : *GeometryArr)
	{
		const TSharedPtr<FJsonObject> GeometryObj = GeometryVal->AsObject();
		if (!GeometryObj.IsValid())
			continue;

		const FString Path = GetStr(GeometryObj, TEXT("path"));
		if (Path.IsEmpty())
			continue;

		OutPath = Path;
		OutRule = GetStr(GeometryObj, TEXT("windingRule"));
		return true;
	}

	return false;
}

struct FNodeBounds
{
	float X = 0.0f;
	float Y = 0.0f;
	float W = 0.0f;
	float H = 0.0f;
};

static bool TryReadBounds(const TSharedPtr<FJsonObject>& BoundsObj, FNodeBounds& OutBounds)
{
	if (!BoundsObj.IsValid())
	{
		return false;
	}

	OutBounds.X = GetNum(BoundsObj, TEXT("x"));
	OutBounds.Y = GetNum(BoundsObj, TEXT("y"));
	OutBounds.W = GetNum(BoundsObj, TEXT("width"));
	OutBounds.H = GetNum(BoundsObj, TEXT("height"));
	return OutBounds.W > 0.0f && OutBounds.H > 0.0f;
}

static bool HasVisibleStroke(const TSharedPtr<FJsonObject>& NodeObj)
{
	const TArray<TSharedPtr<FJsonValue>>* Strokes = GetArr(NodeObj, TEXT("strokes"));
	if (!Strokes || Strokes->Num() == 0)
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& StrokeVal : *Strokes)
	{
		const TSharedPtr<FJsonObject> StrokeObj = StrokeVal->AsObject();
		if (StrokeObj.IsValid() && GetBool(StrokeObj, TEXT("visible"), true))
		{
			return true;
		}
	}

	return false;
}

static void GetStrokeExpansion(const TSharedPtr<FJsonObject>& NodeObj, float& OutLeft, float& OutRight, float& OutTop, float& OutBottom)
{
	OutLeft = 0.0f;
	OutRight = 0.0f;
	OutTop = 0.0f;
	OutBottom = 0.0f;

	if (!HasVisibleStroke(NodeObj))
	{
		return;
	}

	float Left = 0.0f;
	float Right = 0.0f;
	float Top = 0.0f;
	float Bottom = 0.0f;
	const TSharedPtr<FJsonObject> IndividualWeights = GetObj(NodeObj, TEXT("individualStrokeWeights"));
	if (IndividualWeights.IsValid())
	{
		Top = GetNum(IndividualWeights, TEXT("top"));
		Right = GetNum(IndividualWeights, TEXT("right"));
		Bottom = GetNum(IndividualWeights, TEXT("bottom"));
		Left = GetNum(IndividualWeights, TEXT("left"));
	}
	else
	{
		const float Uniform = GetNum(NodeObj, TEXT("strokeWeight"));
		Top = Uniform;
		Right = Uniform;
		Bottom = Uniform;
		Left = Uniform;
	}

	const FString Align = GetStr(NodeObj, TEXT("strokeAlign"), TEXT("INSIDE"));
	if (Align == TEXT("OUTSIDE"))
	{
		OutLeft = Left;
		OutRight = Right;
		OutTop = Top;
		OutBottom = Bottom;
	}
	else if (Align == TEXT("CENTER"))
	{
		OutLeft = Left * 0.5f;
		OutRight = Right * 0.5f;
		OutTop = Top * 0.5f;
		OutBottom = Bottom * 0.5f;
	}
}

static bool TryGetVisualBounds(const TSharedPtr<FJsonObject>& NodeObj, FNodeBounds& OutBounds)
{
	if (!NodeObj.IsValid())
	{
		return false;
	}

	// Prefer Figma visual bounds when present (includes stroke/effects).
	if (TryReadBounds(GetObj(NodeObj, TEXT("absoluteRenderBounds")), OutBounds))
	{
		return true;
	}

	if (!TryReadBounds(GetObj(NodeObj, TEXT("absoluteBoundingBox")), OutBounds))
	{
		return false;
	}

	// Local export may omit absoluteRenderBounds; expand bbox by stroke align as fallback.
	float ExpandLeft = 0.0f;
	float ExpandRight = 0.0f;
	float ExpandTop = 0.0f;
	float ExpandBottom = 0.0f;
	GetStrokeExpansion(NodeObj, ExpandLeft, ExpandRight, ExpandTop, ExpandBottom);

	OutBounds.X -= ExpandLeft;
	OutBounds.Y -= ExpandTop;
	OutBounds.W += ExpandLeft + ExpandRight;
	OutBounds.H += ExpandTop + ExpandBottom;
	return OutBounds.W > 0.0f && OutBounds.H > 0.0f;
}

static void ParsePathGeometry(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	FString GeometryPath;
	FString WindingRule;

	// Prefer fillGeometry (visual fill shape). Fallback to strokeGeometry for stroke-only vectors/lines.
	if (!ParseGeometryArray(GetArr(NodeObj, TEXT("fillGeometry")), GeometryPath, WindingRule))
	{
		ParseGeometryArray(GetArr(NodeObj, TEXT("strokeGeometry")), GeometryPath, WindingRule);
	}

	if (!GeometryPath.IsEmpty())
	{
		OutNode.Geometry = GeometryPath;
		if (!WindingRule.IsEmpty())
		{
			OutNode.FillRule = NormalizeWindingRule(WindingRule);
		}
	}
}

// ---------------------------------------------------------------------------
// MapNodeType
// ---------------------------------------------------------------------------

EPenNodeType FFigmaParser::MapNodeType(const FString& FigmaType)
{
	if (FigmaType == TEXT("FRAME") || FigmaType == TEXT("COMPONENT_SET") || FigmaType == TEXT("SECTION"))
		return EPenNodeType::Frame;
	if (FigmaType == TEXT("COMPONENT"))
		return EPenNodeType::Frame;
	if (FigmaType == TEXT("INSTANCE"))
		return EPenNodeType::Frame;  // Figma API已展开INSTANCE children，直接当Frame处理
	if (FigmaType == TEXT("GROUP"))
		return EPenNodeType::Group;
	if (FigmaType == TEXT("RECTANGLE"))
		return EPenNodeType::Rectangle;
	if (FigmaType == TEXT("ELLIPSE"))
		return EPenNodeType::Ellipse;
	if (FigmaType == TEXT("LINE"))
		return EPenNodeType::Line;
	if (FigmaType == TEXT("REGULAR_POLYGON") || FigmaType == TEXT("STAR"))
		return EPenNodeType::Polygon;
	if (FigmaType == TEXT("VECTOR") || FigmaType == TEXT("BOOLEAN_OPERATION"))
		return EPenNodeType::Path;
	if (FigmaType == TEXT("TEXT"))
		return EPenNodeType::Text;
	return EPenNodeType::Unknown;
}

// ---------------------------------------------------------------------------
// HasAutoLayout
// ---------------------------------------------------------------------------

bool FFigmaParser::HasAutoLayout(const TSharedPtr<FJsonObject>& NodeObj)
{
	FString Mode = GetStr(NodeObj, TEXT("layoutMode"));
	return Mode == TEXT("HORIZONTAL") || Mode == TEXT("VERTICAL");
}

// ---------------------------------------------------------------------------
// ColorToHex
// ---------------------------------------------------------------------------

FString FFigmaParser::ColorToHex(const TSharedPtr<FJsonObject>& ColorObj, float Opacity)
{
	if (!ColorObj) return TEXT("#000000");

	uint8 R = FMath::Clamp(FMath::RoundToInt(GetNum(ColorObj, TEXT("r")) * 255.0), 0, 255);
	uint8 G = FMath::Clamp(FMath::RoundToInt(GetNum(ColorObj, TEXT("g")) * 255.0), 0, 255);
	uint8 B = FMath::Clamp(FMath::RoundToInt(GetNum(ColorObj, TEXT("b")) * 255.0), 0, 255);
	float A = GetNum(ColorObj, TEXT("a"), 1.0) * Opacity;
	uint8 AI = FMath::Clamp(FMath::RoundToInt(A * 255.0), 0, 255);

	if (AI >= 255)
		return FString::Printf(TEXT("#%02X%02X%02X"), R, G, B);
	return FString::Printf(TEXT("#%02X%02X%02X%02X"), R, G, B, AI);
}

// ---------------------------------------------------------------------------
// ParseLayout
// ---------------------------------------------------------------------------

void FFigmaParser::ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	FString Mode = GetStr(NodeObj, TEXT("layoutMode"));
	if (Mode == TEXT("HORIZONTAL"))
		OutNode.Layout = EPenLayout::Horizontal;
	else if (Mode == TEXT("VERTICAL"))
		OutNode.Layout = EPenLayout::Vertical;
	else
		OutNode.Layout = EPenLayout::None;

	OutNode.Gap = GetNum(NodeObj, TEXT("itemSpacing"));

	OutNode.Padding.Left = GetNum(NodeObj, TEXT("paddingLeft"));
	OutNode.Padding.Right = GetNum(NodeObj, TEXT("paddingRight"));
	OutNode.Padding.Top = GetNum(NodeObj, TEXT("paddingTop"));
	OutNode.Padding.Bottom = GetNum(NodeObj, TEXT("paddingBottom"));

	FString Primary = GetStr(NodeObj, TEXT("primaryAxisAlignItems"));
	if (Primary == TEXT("CENTER"))
		OutNode.JustifyContent = EPenJustifyContent::Center;
	else if (Primary == TEXT("MAX"))
		OutNode.JustifyContent = EPenJustifyContent::End;
	else if (Primary == TEXT("SPACE_BETWEEN"))
		OutNode.JustifyContent = EPenJustifyContent::SpaceBetween;
	else
		OutNode.JustifyContent = EPenJustifyContent::Start;

	FString Counter = GetStr(NodeObj, TEXT("counterAxisAlignItems"));
	if (Counter == TEXT("CENTER"))
		OutNode.AlignItems = EPenAlignItems::Center;
	else if (Counter == TEXT("MAX"))
		OutNode.AlignItems = EPenAlignItems::End;
	else
		OutNode.AlignItems = EPenAlignItems::Start;

	OutNode.bHasClip = true;
	OutNode.bClip = GetBool(NodeObj, TEXT("clipsContent"), false);
}

// ---------------------------------------------------------------------------
// ParseSize
// ---------------------------------------------------------------------------

void FFigmaParser::ParseSize(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	FNodeBounds VisualBounds;
	FNodeBounds LayoutBounds;
	const bool bHasVisualBounds = TryGetVisualBounds(NodeObj, VisualBounds);
	const bool bHasLayoutBounds = TryReadBounds(GetObj(NodeObj, TEXT("absoluteBoundingBox")), LayoutBounds);

	// Auto layout should remain in layout space; absolute layout uses visual bounds.
	const FNodeBounds& EffectiveBounds = bParentHasAutoLayout
		? (bHasLayoutBounds ? LayoutBounds : VisualBounds)
		: (bHasVisualBounds ? VisualBounds : LayoutBounds);

	float W = EffectiveBounds.W;
	float H = EffectiveBounds.H;

	if (bParentHasAutoLayout)
	{
		FString SizingH = GetStr(NodeObj, TEXT("layoutSizingHorizontal"), TEXT("FIXED"));
		if (SizingH == TEXT("FILL"))
			OutNode.Width = FPenSize::Fill(W);
		else if (SizingH == TEXT("HUG"))
			OutNode.Width = FPenSize::Fit(W);
		else
			OutNode.Width = FPenSize::Fixed(W);

		FString SizingV = GetStr(NodeObj, TEXT("layoutSizingVertical"), TEXT("FIXED"));
		if (SizingV == TEXT("FILL"))
			OutNode.Height = FPenSize::Fill(H);
		else if (SizingV == TEXT("HUG"))
			OutNode.Height = FPenSize::Fit(H);
		else
			OutNode.Height = FPenSize::Fixed(H);
	}
	else
	{
		OutNode.Width = FPenSize::Fixed(W);
		OutNode.Height = FPenSize::Fixed(H);
	}

	// Figma layoutGrow: 1.0 = fill, 0.0 = fixed (only meaningful in auto-layout)
	double GrowVal = 0.0;
	if (NodeObj->TryGetNumberField(TEXT("layoutGrow"), GrowVal))
		OutNode.LayoutGrow = static_cast<float>(GrowVal);

	OutNode.bHasWidth = true;
	OutNode.bHasHeight = true;
}

// ---------------------------------------------------------------------------
// ParseSingleFill - 解析单个 Figma fill 对象
// ---------------------------------------------------------------------------

FPenFill FFigmaParser::ParseSingleFill(const TSharedPtr<FJsonObject>& FillObj)
{
	FPenFill Result;
	if (!FillObj || !GetBool(FillObj, TEXT("visible"), true))
		return Result;

	FString FillType = GetStr(FillObj, TEXT("type"));
	float FillOpacity = GetNum(FillObj, TEXT("opacity"), 1.0);

	if (FillType == TEXT("SOLID"))
	{
		auto ColorObj = GetObj(FillObj, TEXT("color"));
		Result = FPenFill::FromColor(ColorToHex(ColorObj, FillOpacity));
		return Result;
	}

	if (FillType == TEXT("GRADIENT_LINEAR") || FillType == TEXT("GRADIENT_RADIAL") || FillType == TEXT("GRADIENT_ANGULAR") || FillType == TEXT("GRADIENT_DIAMOND"))
	{
		Result.Type = EPenFillType::Gradient;

		if (FillType == TEXT("GRADIENT_LINEAR"))
			Result.Gradient.GradientType = EPenGradientType::Linear;
		else if (FillType == TEXT("GRADIENT_RADIAL") || FillType == TEXT("GRADIENT_DIAMOND"))
			Result.Gradient.GradientType = EPenGradientType::Radial;
		else
			Result.Gradient.GradientType = EPenGradientType::Angular;

		const auto* Handles = GetArr(FillObj, TEXT("gradientHandlePositions"));
		if (Handles && Handles->Num() >= 2)
		{
			auto P0 = (*Handles)[0]->AsObject();
			auto P1 = (*Handles)[1]->AsObject();
			if (P0 && P1)
			{
				float DX = GetNum(P1, TEXT("x")) - GetNum(P0, TEXT("x"));
				float DY = GetNum(P1, TEXT("y")) - GetNum(P0, TEXT("y"));
				Result.Gradient.Rotation = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));
			}
		}

		const auto* Stops = GetArr(FillObj, TEXT("gradientStops"));
		if (Stops)
		{
			for (const auto& StopVal : *Stops)
			{
				auto StopObj = StopVal->AsObject();
				if (!StopObj) continue;

				FPenGradientStop Stop;
				Stop.Color = ColorToHex(GetObj(StopObj, TEXT("color")), FillOpacity);
				Stop.Position = GetNum(StopObj, TEXT("position"));
				Result.Gradient.Colors.Add(Stop);
			}
		}
		return Result;
	}

	if (FillType == TEXT("IMAGE"))
	{
		Result.Type = EPenFillType::Image;
		Result.Image.Url = GetStr(FillObj, TEXT("imageRef"));
		Result.Image.Opacity = FillOpacity;

		FString ScaleMode = GetStr(FillObj, TEXT("scaleMode"));
		if (ScaleMode == TEXT("FILL"))
			Result.Image.Mode = EPenImageMode::Fill;
		else if (ScaleMode == TEXT("FIT"))
			Result.Image.Mode = EPenImageMode::Fit;
		else if (ScaleMode == TEXT("CROP"))
			Result.Image.Mode = EPenImageMode::Crop;
		else
			Result.Image.Mode = EPenImageMode::Stretch;

		// 解析 imageTransform（2x3 仿射矩阵 [[a,b,tx],[c,d,ty]]）
		const auto* TransformArr = GetArr(FillObj, TEXT("imageTransform"));
		if (TransformArr && TransformArr->Num() == 2)
		{
			auto Row0 = (*TransformArr)[0]->AsArray();
			auto Row1 = (*TransformArr)[1]->AsArray();
			if (Row0.Num() == 3 && Row1.Num() == 3)
			{
				Result.Image.Transform.A  = Row0[0]->AsNumber();
				Result.Image.Transform.B  = Row0[1]->AsNumber();
				Result.Image.Transform.TX = Row0[2]->AsNumber();
				Result.Image.Transform.C  = Row1[0]->AsNumber();
				Result.Image.Transform.D  = Row1[1]->AsNumber();
				Result.Image.Transform.TY = Row1[2]->AsNumber();
			}
		}

		return Result;
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ParseFills - 收集所有可见填充层（底→顶顺序）
// ---------------------------------------------------------------------------

TArray<FPenFill> FFigmaParser::ParseFills(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	TArray<FPenFill> Result;
	for (int32 i = 0; i < Fills.Num(); ++i)
	{
		auto FillObj = Fills[i]->AsObject();
		if (!FillObj || !GetBool(FillObj, TEXT("visible"), true))
			continue;

		FPenFill Fill = ParseSingleFill(FillObj);
		if (Fill.Type != EPenFillType::None)
			Result.Add(MoveTemp(Fill));
	}
	return Result;
}

// ---------------------------------------------------------------------------
// ParseFill - 返回最顶层可见填充（向后兼容）
// ---------------------------------------------------------------------------

FPenFill FFigmaParser::ParseFill(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	// Take the last visible fill (Figma renders last on top)
	for (int32 i = Fills.Num() - 1; i >= 0; --i)
	{
		auto FillObj = Fills[i]->AsObject();
		if (!FillObj || !GetBool(FillObj, TEXT("visible"), true))
			continue;

		FPenFill Fill = ParseSingleFill(FillObj);
		if (Fill.Type != EPenFillType::None)
			return Fill;
	}
	return FPenFill();
}

// ---------------------------------------------------------------------------
// ParseStroke
// ---------------------------------------------------------------------------

FPenStroke FFigmaParser::ParseStroke(const TSharedPtr<FJsonObject>& NodeObj)
{
	FPenStroke Result;

	const auto* Strokes = GetArr(NodeObj, TEXT("strokes"));
	if (Strokes)
	{
		for (const auto& StrokeVal : *Strokes)
		{
			auto StrokeObj = StrokeVal->AsObject();
			if (!StrokeObj || !GetBool(StrokeObj, TEXT("visible"), true))
				continue;

			auto ColorObj = GetObj(StrokeObj, TEXT("color"));
			float StrokeOpacity = GetNum(StrokeObj, TEXT("opacity"), 1.0);
			Result.Fill = FPenFill::FromColor(ColorToHex(ColorObj, StrokeOpacity));
			break;
		}
	}

	// Stroke align
	FString Align = GetStr(NodeObj, TEXT("strokeAlign"), TEXT("INSIDE"));
	if (Align == TEXT("OUTSIDE"))
		Result.Align = EPenStrokeAlign::Outside;
	else if (Align == TEXT("CENTER"))
		Result.Align = EPenStrokeAlign::Center;
	else
		Result.Align = EPenStrokeAlign::Inside;

	// Individual stroke weights
	auto IndividualWeights = GetObj(NodeObj, TEXT("individualStrokeWeights"));
	if (IndividualWeights)
	{
		Result.Thickness.bUniform = false;
		Result.Thickness.Top = GetNum(IndividualWeights, TEXT("top"));
		Result.Thickness.Right = GetNum(IndividualWeights, TEXT("right"));
		Result.Thickness.Bottom = GetNum(IndividualWeights, TEXT("bottom"));
		Result.Thickness.Left = GetNum(IndividualWeights, TEXT("left"));
	}
	else
	{
		Result.Thickness.bUniform = true;
		Result.Thickness.Uniform = GetNum(NodeObj, TEXT("strokeWeight"));
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ParseCornerRadius
// ---------------------------------------------------------------------------

FPenCornerRadius FFigmaParser::ParseCornerRadius(const TSharedPtr<FJsonObject>& NodeObj)
{
	FPenCornerRadius Result;

	const auto* Radii = GetArr(NodeObj, TEXT("rectangleCornerRadii"));
	if (Radii && Radii->Num() == 4)
	{
		Result.bUniform = false;
		Result.TopLeft = (*Radii)[0]->AsNumber();
		Result.TopRight = (*Radii)[1]->AsNumber();
		Result.BottomRight = (*Radii)[2]->AsNumber();
		Result.BottomLeft = (*Radii)[3]->AsNumber();
	}
	else
	{
		Result.bUniform = true;
		Result.Uniform = GetNum(NodeObj, TEXT("cornerRadius"));
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ParseEffects
// ---------------------------------------------------------------------------

TArray<FPenEffect> FFigmaParser::ParseEffects(const TArray<TSharedPtr<FJsonValue>>& Effects)
{
	TArray<FPenEffect> Result;

	for (const auto& EffVal : Effects)
	{
		auto EffObj = EffVal->AsObject();
		if (!EffObj || !GetBool(EffObj, TEXT("visible"), true))
			continue;

		FString EffType = GetStr(EffObj, TEXT("type"));
		FPenEffect Eff;

		if (EffType == TEXT("DROP_SHADOW") || EffType == TEXT("INNER_SHADOW"))
		{
			Eff.Type = EPenEffectType::Shadow;
			Eff.Shadow.bInner = (EffType == TEXT("INNER_SHADOW"));

			auto Offset = GetObj(EffObj, TEXT("offset"));
			Eff.Shadow.OffsetX = Offset ? GetNum(Offset, TEXT("x")) : 0.0f;
			Eff.Shadow.OffsetY = Offset ? GetNum(Offset, TEXT("y")) : 0.0f;
			Eff.Shadow.Blur = GetNum(EffObj, TEXT("radius"));
			Eff.Shadow.Spread = GetNum(EffObj, TEXT("spread"));
			float ShadowOpacity = GetNum(EffObj, TEXT("opacity"), 1.0);
			Eff.Shadow.Color = ColorToHex(GetObj(EffObj, TEXT("color")), ShadowOpacity);
		}
		else if (EffType == TEXT("LAYER_BLUR"))
		{
			Eff.Type = EPenEffectType::Blur;
			Eff.Radius = GetNum(EffObj, TEXT("radius"));
		}
		else if (EffType == TEXT("BACKGROUND_BLUR"))
		{
			Eff.Type = EPenEffectType::BackgroundBlur;
			Eff.Radius = GetNum(EffObj, TEXT("radius"));
		}
		else
		{
			continue;
		}

		Result.Add(Eff);
	}

	return Result;
}

// ---------------------------------------------------------------------------
// ParseTextProperties
// ---------------------------------------------------------------------------

void FFigmaParser::ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& OutNode)
{
	OutNode.Content = GetStr(NodeObj, TEXT("characters"));

	auto Style = GetObj(NodeObj, TEXT("style"));
	if (!Style) return;

	OutNode.FontFamily = GetStr(Style, TEXT("fontFamily"));
	OutNode.FontSize = GetNum(Style, TEXT("fontSize"), 16.0);

	int32 Weight = FMath::RoundToInt(GetNum(Style, TEXT("fontWeight"), 400.0));
	// Snap to nearest standard weight (100-900)
	Weight = FMath::Clamp(FMath::RoundToInt(Weight / 100.0f) * 100, 100, 900);
	OutNode.FontWeight = FString::FromInt(Weight);

	FString LineHeightUnit = GetStr(Style, TEXT("lineHeightUnit"));
	if (LineHeightUnit != TEXT("AUTO"))
	{
		// UMG TextBlock::LineHeightPercentage expects a ratio (e.g. 1.2),
		// while Figma provides px or percent-based values.
		if (LineHeightUnit == TEXT("PIXELS"))
		{
			const float LineHeightPx = GetNum(Style, TEXT("lineHeightPx"));
			if (LineHeightPx > 0.0f && OutNode.FontSize > 0.0f)
				OutNode.LineHeight = LineHeightPx / OutNode.FontSize;
		}
		else if (LineHeightUnit == TEXT("FONT_SIZE_%"))
		{
			const float Percent = GetNum(Style, TEXT("lineHeightPercentFontSize"));
			if (Percent > 0.0f)
				OutNode.LineHeight = Percent / 100.0f;
		}
		else
		{
			// Fallback for less common payloads.
			const float Percent = GetNum(Style, TEXT("lineHeightPercentFontSize"));
			if (Percent > 0.0f)
				OutNode.LineHeight = Percent / 100.0f;
		}
	}

	OutNode.LetterSpacing = GetNum(Style, TEXT("letterSpacing"));

	FString HAlign = GetStr(Style, TEXT("textAlignHorizontal"), TEXT("LEFT"));
	if (HAlign == TEXT("CENTER"))
		OutNode.TextAlign = EPenTextAlign::Center;
	else if (HAlign == TEXT("RIGHT"))
		OutNode.TextAlign = EPenTextAlign::Right;
	else if (HAlign == TEXT("JUSTIFIED"))
		OutNode.TextAlign = EPenTextAlign::Justify;
	else
		OutNode.TextAlign = EPenTextAlign::Left;

	FString VAlign = GetStr(Style, TEXT("textAlignVertical"), TEXT("TOP"));
	if (VAlign == TEXT("CENTER"))
		OutNode.TextAlignVertical = EPenTextAlignVertical::Middle;
	else if (VAlign == TEXT("BOTTOM"))
		OutNode.TextAlignVertical = EPenTextAlignVertical::Bottom;
	else
		OutNode.TextAlignVertical = EPenTextAlignVertical::Top;

	// textAutoResize can appear on node root or inside style (nodes API payload).
	FString AutoResize = GetStr(NodeObj, TEXT("textAutoResize"));
	if (AutoResize.IsEmpty())
	{
		AutoResize = GetStr(Style, TEXT("textAutoResize"), TEXT("NONE"));
	}
	if (AutoResize == TEXT("WIDTH_AND_HEIGHT"))
		OutNode.TextGrowth = EPenTextGrowth::Auto;
	else if (AutoResize == TEXT("HEIGHT"))
		OutNode.TextGrowth = EPenTextGrowth::FixedWidth;
	else
		OutNode.TextGrowth = EPenTextGrowth::FixedWidthHeight;
}

// ---------------------------------------------------------------------------
// ParsePosition
// ---------------------------------------------------------------------------

void FFigmaParser::ParsePosition(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FDesignNode& OutNode, bool bParentHasAutoLayout)
{
	if (bParentHasAutoLayout)
	{
		OutNode.bHasX = false;
		OutNode.bHasY = false;
		return;
	}

	FNodeBounds ChildBounds;
	const bool bHasChildBounds = TryGetVisualBounds(NodeObj, ChildBounds);

	FNodeBounds ParentBounds;
	const bool bHasParentBounds = ParentObj && TryGetVisualBounds(ParentObj, ParentBounds);

	if (bHasChildBounds && bHasParentBounds)
	{
		OutNode.X = ChildBounds.X - ParentBounds.X;
		OutNode.Y = ChildBounds.Y - ParentBounds.Y;
		OutNode.bHasX = true;
		OutNode.bHasY = true;
	}
	else if (bHasChildBounds)
	{
		OutNode.X = ChildBounds.X;
		OutNode.Y = ChildBounds.Y;
		OutNode.bHasX = true;
		OutNode.bHasY = true;
	}
}

// ---------------------------------------------------------------------------
// ParseNode (recursive)
// ---------------------------------------------------------------------------

TSharedPtr<FDesignNode> FFigmaParser::ParseNode(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj)
{
	if (!NodeObj) return nullptr;

	FString FigmaType = GetStr(NodeObj, TEXT("type"));
	if (FigmaType.IsEmpty()) return nullptr;

	// Figma mask 节点是裁剪几何，不应直接生成可视控件。
	const bool bIsMaskNode = GetBool(NodeObj, TEXT("isMask"), false) || !GetStr(NodeObj, TEXT("maskType")).IsEmpty();
	if (bIsMaskNode)
	{
		return nullptr;
	}

	// 本地导出数据可能丢失 isMask 字段：对典型“黑色同尺寸遮罩矩形”做保守兜底。
	if (ParentObj &&
		FigmaType == TEXT("RECTANGLE") &&
		GetStr(NodeObj, TEXT("name")) == TEXT("Rectangle") &&
		IsVisibleSolidBlackFill(NodeObj) &&
		HasSameBoundsAsParent(NodeObj, ParentObj))
	{
		const bool bParentClips = GetBool(ParentObj, TEXT("clipsContent"), false);
		const bool bParentGroup = (GetStr(ParentObj, TEXT("type")) == TEXT("GROUP"));
		if (bParentClips || bParentGroup)
		{
			return nullptr;
		}
	}

	auto Node = MakeShared<FDesignNode>();
	Node->Id = GetStr(NodeObj, TEXT("id"));
	Node->Name = GetStr(NodeObj, TEXT("name"));
	Node->bEnabled = GetBool(NodeObj, TEXT("visible"), true);
	Node->Opacity = GetNum(NodeObj, TEXT("opacity"), 1.0);
	Node->Rotation = GetNum(NodeObj, TEXT("rotation"));

	// relativeTransform → flipX/flipY
	// 矩阵 [[a, b, tx], [c, d, ty]]，用行列式(ad-bc)判断是否有镜像翻转
	// 纯旋转时 det=1，单轴翻转时 det=-1，避免旋转角>90°时cos为负被误判
	const auto* TransformArr = GetArr(NodeObj, TEXT("relativeTransform"));
	if (TransformArr && TransformArr->Num() == 2)
	{
		auto Row0 = (*TransformArr)[0]->AsArray();
		auto Row1 = (*TransformArr)[1]->AsArray();
		if (Row0.Num() == 3 && Row1.Num() == 3)
		{
			float A = Row0[0]->AsNumber();  // cos or -cos(flipped)
			float B = Row0[1]->AsNumber();  // -sin
			float C = Row1[0]->AsNumber();  // sin
			float D = Row1[1]->AsNumber();  // cos or -cos(flipped)
			float Det = A * D - B * C;
			// det < 0 表示有一次镜像翻转
			// 通过分解判断是X翻转还是Y翻转：
			// 去掉旋转分量后，scaleX = sqrt(a²+c²), scaleY = sqrt(b²+d²)
			// 如果 det < 0，翻转轴由 atan2(c,a) vs atan2(-b,d) 的一致性决定
			if (Det < -KINDA_SMALL_NUMBER)
			{
				float ScaleX = FMath::Sqrt(A * A + C * C);
				float ScaleY = FMath::Sqrt(B * B + D * D);
				// Figma 通常只做单轴翻转，用 a 的符号区分：
				// 如果去旋转后 a/scaleX 接近 -1 → X翻转，否则 Y翻转
				float CosFromX = A / FMath::Max(ScaleX, KINDA_SMALL_NUMBER);
				float CosFromY = D / FMath::Max(ScaleY, KINDA_SMALL_NUMBER);
				// 两个cos应该同号（来自同一旋转角），如果异号说明其中一个被翻转
				Node->bFlipX = (CosFromX * CosFromY < 0.0f) ? (CosFromX < 0.0f) : false;
				Node->bFlipY = (CosFromX * CosFromY < 0.0f) ? (CosFromY < 0.0f) : true;
			}
		}
	}

	Node->Type = MapNodeType(FigmaType);

	if (FigmaType == TEXT("COMPONENT"))
		Node->bReusable = true;

	// INSTANCE: 记录componentId作为元数据（不走Ref展开，Figma API已展开children）
	if (FigmaType == TEXT("INSTANCE"))
	{
		Node->Ref = GetStr(NodeObj, TEXT("componentId"));
	}

	bool bParentAutoLayout = ParentObj ? HasAutoLayout(ParentObj) : false;

	// Position
	ParsePosition(NodeObj, ParentObj, *Node, bParentAutoLayout);

	// Size
	ParseSize(NodeObj, *Node, bParentAutoLayout);

	// Layout (for containers)
	ParseLayout(NodeObj, *Node);

	// Fills
	const auto* FillsArr = GetArr(NodeObj, TEXT("fills"));
	if (FillsArr)
	{
		Node->Fills = ParseFills(*FillsArr);
		Node->Fill = ParseFill(*FillsArr);
	}

	// Stroke
	Node->Stroke = ParseStroke(NodeObj);

	// Vector geometry (for VECTOR / BOOLEAN_OPERATION / LINE / etc.)
	if (Node->Type == EPenNodeType::Path
		|| Node->Type == EPenNodeType::Line
		|| Node->Type == EPenNodeType::Polygon
		|| Node->Type == EPenNodeType::Ellipse)
	{
		ParsePathGeometry(NodeObj, *Node);
	}

	// Corner radius
	Node->CornerRadius = ParseCornerRadius(NodeObj);

	// Effects
	const auto* EffectsArr = GetArr(NodeObj, TEXT("effects"));
	if (EffectsArr)
		Node->Effects = ParseEffects(*EffectsArr);

	// Text
	if (Node->Type == EPenNodeType::Text)
		ParseTextProperties(NodeObj, *Node);

	// Children — skip for BOOLEAN_OPERATION/VECTOR (operands, not visual children)
	if (FigmaType != TEXT("BOOLEAN_OPERATION") && FigmaType != TEXT("VECTOR"))
	{
		const auto* ChildrenArr = GetArr(NodeObj, TEXT("children"));
		if (ChildrenArr)
		{
			for (const auto& ChildVal : *ChildrenArr)
			{
				auto ChildObj = ChildVal->AsObject();
				if (auto ChildNode = ParseNode(ChildObj, NodeObj))
					Node->Children.Add(ChildNode);
			}
		}
	}

	return Node;
}

// ---------------------------------------------------------------------------
// ParseDocument
// ---------------------------------------------------------------------------

bool FFigmaParser::ParseDocument(const FString& FigmaJson, FDesignDocument& OutDocument)
{
	auto Root = ParseJsonString(FigmaJson);
	if (!Root)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("FigmaParser: Failed to parse JSON"));
		return false;
	}

	auto DocObj = GetObj(Root, TEXT("document"));
	if (!DocObj)
	{
		// Fallback: Figma nodes API payload (e.g. /v1/files/{key}/nodes?...),
		// structure is usually Root.nodes["node-id"].document.
		const TSharedPtr<FJsonObject> NodesObj = GetObj(Root, TEXT("nodes"));
		if (NodesObj.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : NodesObj->Values)
			{
				const TSharedPtr<FJsonObject> NodeEntry = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
				if (!NodeEntry.IsValid())
					continue;

				DocObj = GetObj(NodeEntry, TEXT("document"));
				if (DocObj.IsValid())
					break;
			}
		}
	}

	if (!DocObj)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("FigmaParser: Missing 'document' field"));
		return false;
	}

	const FString DocType = GetStr(DocObj, TEXT("type"));

	// Standard file payload: DOCUMENT -> pages -> frames.
	if (DocType == TEXT("DOCUMENT"))
	{
		const auto* Pages = GetArr(DocObj, TEXT("children"));
		if (!Pages)
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: No pages found"));
			return true;
		}

		for (const auto& PageVal : *Pages)
		{
			auto PageObj = PageVal->AsObject();
			if (!PageObj) continue;

			const auto* Frames = GetArr(PageObj, TEXT("children"));
			if (!Frames) continue;

			for (const auto& FrameVal : *Frames)
			{
				auto FrameObj = FrameVal->AsObject();
				if (auto Node = ParseNode(FrameObj, nullptr))
					OutDocument.Children.Add(Node);
			}
		}
	}
	// Nodes API may directly return FRAME / CANVAS / COMPONENT roots.
	else if (DocType == TEXT("CANVAS"))
	{
		const auto* Frames = GetArr(DocObj, TEXT("children"));
		if (Frames)
		{
			for (const auto& FrameVal : *Frames)
			{
				auto FrameObj = FrameVal->AsObject();
				if (auto Node = ParseNode(FrameObj, nullptr))
					OutDocument.Children.Add(Node);
			}
		}
	}
	else
	{
		if (auto Node = ParseNode(DocObj, nullptr))
			OutDocument.Children.Add(Node);
	}

	UE_LOG(LogDesign2UMG, Log, TEXT("FigmaParser: Parsed %d top-level frames"), OutDocument.Children.Num());
	return true;
}

// ---------------------------------------------------------------------------
// ExtractFileKey
// ---------------------------------------------------------------------------

bool FFigmaParser::ExtractFileKey(const FString& UrlOrKey, FString& OutKey)
{
	// Match: https://www.figma.com/design/<key>/... or /file/<key>/...
	FRegexPattern Pattern(TEXT("figma\\.com/(?:design|file)/([a-zA-Z0-9]+)"));
	FRegexMatcher Matcher(Pattern, UrlOrKey);

	if (Matcher.FindNext())
	{
		OutKey = Matcher.GetCaptureGroup(1);
		return true;
	}

	// If no URL pattern matched, treat the whole string as a raw key
	if (!UrlOrKey.Contains(TEXT("/")) && !UrlOrKey.Contains(TEXT(".")))
	{
		OutKey = UrlOrKey;
		return true;
	}

	UE_LOG(LogDesign2UMG, Warning, TEXT("FigmaParser: Could not extract file key from: %s"), *UrlOrKey);
	return false;
}

// ---------------------------------------------------------------------------
// CollectImageRefs / ApplyImageUrls
// ---------------------------------------------------------------------------

void FFigmaParser::CollectImageRefs(const FDesignDocument& Document, TSet<FString>& OutImageRefs)
{
	CollectImageRefsRecursive(Document.Children, OutImageRefs);
}

void FFigmaParser::CollectImageRefsRecursive(const TArray<TSharedPtr<FDesignNode>>& Nodes, TSet<FString>& OutRefs)
{
	for (const auto& Node : Nodes)
	{
		if (!Node) continue;
		// 主填充
		if (Node->Fill.IsImage() && !Node->Fill.Image.Url.IsEmpty())
			OutRefs.Add(Node->Fill.Image.Url);
		// 多层填充
		for (const FPenFill& F : Node->Fills)
		{
			if (F.IsImage() && !F.Image.Url.IsEmpty())
				OutRefs.Add(F.Image.Url);
		}
		CollectImageRefsRecursive(Node->Children, OutRefs);
	}
}

void FFigmaParser::ApplyImageUrls(FDesignDocument& Document, const TMap<FString, FString>& ImageRefToUrl)
{
	ApplyImageUrlsRecursive(Document.Children, ImageRefToUrl);
}

void FFigmaParser::ApplyImageUrlsRecursive(TArray<TSharedPtr<FDesignNode>>& Nodes, const TMap<FString, FString>& Map)
{
	for (auto& Node : Nodes)
	{
		if (!Node) continue;
		// 主填充
		if (Node->Fill.IsImage() && !Node->Fill.Image.Url.IsEmpty())
		{
			if (const FString* Url = Map.Find(Node->Fill.Image.Url))
				Node->Fill.Image.Url = *Url;
		}
		// 多层填充
		for (FPenFill& F : Node->Fills)
		{
			if (F.IsImage() && !F.Image.Url.IsEmpty())
			{
				if (const FString* Url = Map.Find(F.Image.Url))
					F.Image.Url = *Url;
			}
		}
		ApplyImageUrlsRecursive(Node->Children, Map);
	}
}
