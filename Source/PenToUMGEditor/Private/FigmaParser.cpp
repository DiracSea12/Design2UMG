// Copyright Epic Games, Inc. All Rights Reserved.

#include "FigmaParser.h"
#include "PenToUMGLog.h"
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
		return EPenNodeType::Ref;
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

void FFigmaParser::ParseLayout(const TSharedPtr<FJsonObject>& NodeObj, FPenNode& OutNode)
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

	OutNode.bClip = GetBool(NodeObj, TEXT("clipsContent"), false);
}

// ---------------------------------------------------------------------------
// ParseSize
// ---------------------------------------------------------------------------

void FFigmaParser::ParseSize(const TSharedPtr<FJsonObject>& NodeObj, FPenNode& OutNode, bool bParentHasAutoLayout)
{
	auto BBox = GetObj(NodeObj, TEXT("absoluteBoundingBox"));
	float W = BBox ? GetNum(BBox, TEXT("width")) : 0.0f;
	float H = BBox ? GetNum(BBox, TEXT("height")) : 0.0f;

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

	OutNode.bHasWidth = true;
	OutNode.bHasHeight = true;
}

// ---------------------------------------------------------------------------
// ParseFill
// ---------------------------------------------------------------------------

FPenFill FFigmaParser::ParseFill(const TArray<TSharedPtr<FJsonValue>>& Fills)
{
	// Take the last visible fill (Figma renders last on top)
	for (int32 i = Fills.Num() - 1; i >= 0; --i)
	{
		auto FillObj = Fills[i]->AsObject();
		if (!FillObj || !GetBool(FillObj, TEXT("visible"), true))
			continue;

		FString FillType = GetStr(FillObj, TEXT("type"));
		float FillOpacity = GetNum(FillObj, TEXT("opacity"), 1.0);

		if (FillType == TEXT("SOLID"))
		{
			auto ColorObj = GetObj(FillObj, TEXT("color"));
			FPenFill Fill = FPenFill::FromColor(ColorToHex(ColorObj, FillOpacity));
			return Fill;
		}

		if (FillType == TEXT("GRADIENT_LINEAR") || FillType == TEXT("GRADIENT_RADIAL") || FillType == TEXT("GRADIENT_ANGULAR") || FillType == TEXT("GRADIENT_DIAMOND"))
		{
			FPenFill Fill;
			Fill.Type = EPenFillType::Gradient;

			if (FillType == TEXT("GRADIENT_LINEAR"))
				Fill.Gradient.GradientType = EPenGradientType::Linear;
			else if (FillType == TEXT("GRADIENT_RADIAL"))
				Fill.Gradient.GradientType = EPenGradientType::Radial;
			else
				Fill.Gradient.GradientType = EPenGradientType::Angular;

			// Compute rotation from gradient handle positions
			const auto* Handles = GetArr(FillObj, TEXT("gradientHandlePositions"));
			if (Handles && Handles->Num() >= 2)
			{
				auto P0 = (*Handles)[0]->AsObject();
				auto P1 = (*Handles)[1]->AsObject();
				if (P0 && P1)
				{
					float DX = GetNum(P1, TEXT("x")) - GetNum(P0, TEXT("x"));
					float DY = GetNum(P1, TEXT("y")) - GetNum(P0, TEXT("y"));
					Fill.Gradient.Rotation = FMath::RadiansToDegrees(FMath::Atan2(DY, DX));
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
					Fill.Gradient.Colors.Add(Stop);
				}
			}
			return Fill;
		}

		if (FillType == TEXT("IMAGE"))
		{
			FPenFill Fill;
			Fill.Type = EPenFillType::Image;
			Fill.Image.Url = GetStr(FillObj, TEXT("imageRef"));
			Fill.Image.Opacity = FillOpacity;

			FString ScaleMode = GetStr(FillObj, TEXT("scaleMode"));
			if (ScaleMode == TEXT("FILL"))
				Fill.Image.Mode = EPenImageMode::Fill;
			else if (ScaleMode == TEXT("FIT"))
				Fill.Image.Mode = EPenImageMode::Fit;
			else
				Fill.Image.Mode = EPenImageMode::Stretch;

			return Fill;
		}
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
			Eff.Shadow.Color = ColorToHex(GetObj(EffObj, TEXT("color")));
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

void FFigmaParser::ParseTextProperties(const TSharedPtr<FJsonObject>& NodeObj, FPenNode& OutNode)
{
	OutNode.Content = GetStr(NodeObj, TEXT("characters"));

	auto Style = GetObj(NodeObj, TEXT("style"));
	if (!Style) return;

	OutNode.FontFamily = GetStr(Style, TEXT("fontFamily"));
	OutNode.FontSize = GetNum(Style, TEXT("fontSize"), 16.0);

	int32 Weight = FMath::RoundToInt(GetNum(Style, TEXT("fontWeight"), 400.0));
	if (Weight >= 700)
		OutNode.FontWeight = TEXT("bold");
	else if (Weight >= 500)
		OutNode.FontWeight = TEXT("medium");
	else
		OutNode.FontWeight = TEXT("normal");

	FString LineHeightUnit = GetStr(Style, TEXT("lineHeightUnit"));
	if (LineHeightUnit != TEXT("AUTO"))
		OutNode.LineHeight = GetNum(Style, TEXT("lineHeightPx"));

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
}

// ---------------------------------------------------------------------------
// ParsePosition
// ---------------------------------------------------------------------------

void FFigmaParser::ParsePosition(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj, FPenNode& OutNode, bool bParentHasAutoLayout)
{
	if (bParentHasAutoLayout)
	{
		OutNode.bHasX = false;
		OutNode.bHasY = false;
		return;
	}

	auto ChildBBox = GetObj(NodeObj, TEXT("absoluteBoundingBox"));
	auto ParentBBox = ParentObj ? GetObj(ParentObj, TEXT("absoluteBoundingBox")) : nullptr;

	if (ChildBBox && ParentBBox)
	{
		OutNode.X = GetNum(ChildBBox, TEXT("x")) - GetNum(ParentBBox, TEXT("x"));
		OutNode.Y = GetNum(ChildBBox, TEXT("y")) - GetNum(ParentBBox, TEXT("y"));
		OutNode.bHasX = true;
		OutNode.bHasY = true;
	}
	else if (ChildBBox)
	{
		OutNode.X = GetNum(ChildBBox, TEXT("x"));
		OutNode.Y = GetNum(ChildBBox, TEXT("y"));
		OutNode.bHasX = true;
		OutNode.bHasY = true;
	}
}

// ---------------------------------------------------------------------------
// ParseNode (recursive)
// ---------------------------------------------------------------------------

TSharedPtr<FPenNode> FFigmaParser::ParseNode(const TSharedPtr<FJsonObject>& NodeObj, const TSharedPtr<FJsonObject>& ParentObj)
{
	if (!NodeObj) return nullptr;

	FString FigmaType = GetStr(NodeObj, TEXT("type"));
	if (FigmaType.IsEmpty()) return nullptr;

	auto Node = MakeShared<FPenNode>();
	Node->Id = GetStr(NodeObj, TEXT("id"));
	Node->Name = GetStr(NodeObj, TEXT("name"));
	Node->bEnabled = GetBool(NodeObj, TEXT("visible"), true);
	Node->Opacity = GetNum(NodeObj, TEXT("opacity"), 1.0);
	Node->Rotation = GetNum(NodeObj, TEXT("rotation"));

	Node->Type = MapNodeType(FigmaType);

	if (FigmaType == TEXT("COMPONENT"))
		Node->bReusable = true;

	// INSTANCE → Ref with componentId
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
		Node->Fill = ParseFill(*FillsArr);

	// Stroke
	Node->Stroke = ParseStroke(NodeObj);

	// Corner radius
	Node->CornerRadius = ParseCornerRadius(NodeObj);

	// Effects
	const auto* EffectsArr = GetArr(NodeObj, TEXT("effects"));
	if (EffectsArr)
		Node->Effects = ParseEffects(*EffectsArr);

	// Text
	if (Node->Type == EPenNodeType::Text)
		ParseTextProperties(NodeObj, *Node);

	// Children
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

	return Node;
}

// ---------------------------------------------------------------------------
// ParseDocument
// ---------------------------------------------------------------------------

bool FFigmaParser::ParseDocument(const FString& FigmaJson, FPenDocument& OutDocument)
{
	auto Root = ParseJsonString(FigmaJson);
	if (!Root)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("FigmaParser: Failed to parse JSON"));
		return false;
	}

	auto DocObj = GetObj(Root, TEXT("document"));
	if (!DocObj)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("FigmaParser: Missing 'document' field"));
		return false;
	}

	// Flatten: collect top-level frames from all pages
	const auto* Pages = GetArr(DocObj, TEXT("children"));
	if (!Pages)
	{
		UE_LOG(LogPenToUMG, Warning, TEXT("FigmaParser: No pages found"));
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

	UE_LOG(LogPenToUMG, Log, TEXT("FigmaParser: Parsed %d top-level frames"), OutDocument.Children.Num());
	return true;
}

// ---------------------------------------------------------------------------
// ParseNodes
// ---------------------------------------------------------------------------

bool FFigmaParser::ParseNodes(const FString& NodesJson, FPenDocument& OutDocument)
{
	auto Root = ParseJsonString(NodesJson);
	if (!Root)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("FigmaParser: Failed to parse nodes JSON"));
		return false;
	}

	auto NodesObj = GetObj(Root, TEXT("nodes"));
	if (!NodesObj)
	{
		UE_LOG(LogPenToUMG, Error, TEXT("FigmaParser: Missing 'nodes' field"));
		return false;
	}

	for (const auto& Pair : NodesObj->Values)
	{
		auto EntryObj = Pair.Value->AsObject();
		if (!EntryObj) continue;

		auto DocObj = GetObj(EntryObj, TEXT("document"));
		if (auto Node = ParseNode(DocObj, nullptr))
			OutDocument.Children.Add(Node);
	}

	UE_LOG(LogPenToUMG, Log, TEXT("FigmaParser: Parsed %d nodes"), OutDocument.Children.Num());
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

	UE_LOG(LogPenToUMG, Warning, TEXT("FigmaParser: Could not extract file key from: %s"), *UrlOrKey);
	return false;
}

// ---------------------------------------------------------------------------
// CollectImageRefs / ApplyImageUrls
// ---------------------------------------------------------------------------

void FFigmaParser::CollectImageRefs(const FPenDocument& Document, TSet<FString>& OutImageRefs)
{
	CollectImageRefsRecursive(Document.Children, OutImageRefs);
}

void FFigmaParser::CollectImageRefsRecursive(const TArray<TSharedPtr<FPenNode>>& Nodes, TSet<FString>& OutRefs)
{
	for (const auto& Node : Nodes)
	{
		if (!Node) continue;
		if (Node->Fill.IsImage() && !Node->Fill.Image.Url.IsEmpty())
			OutRefs.Add(Node->Fill.Image.Url);
		CollectImageRefsRecursive(Node->Children, OutRefs);
	}
}

void FFigmaParser::ApplyImageUrls(FPenDocument& Document, const TMap<FString, FString>& ImageRefToUrl)
{
	ApplyImageUrlsRecursive(Document.Children, ImageRefToUrl);
}

void FFigmaParser::ApplyImageUrlsRecursive(TArray<TSharedPtr<FPenNode>>& Nodes, const TMap<FString, FString>& Map)
{
	for (auto& Node : Nodes)
	{
		if (!Node) continue;
		if (Node->Fill.IsImage() && !Node->Fill.Image.Url.IsEmpty())
		{
			if (const FString* Url = Map.Find(Node->Fill.Image.Url))
				Node->Fill.Image.Url = *Url;
		}
		ApplyImageUrlsRecursive(Node->Children, Map);
	}
}
