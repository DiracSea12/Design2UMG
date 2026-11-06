// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FigmaParserMetadata.h"

#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "UMGValueParser.h"
#include "VisualMapperStrokeSupport.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "FigmaParserInternal.h"
#include "FigmaParserLayoutBounds.h"
#include "ParserSharedSemantics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	using DesignJsonUtils::GetString;
	using DesignJsonUtils::TryGetNumber;

	void AddExtensionIfNonEmpty(TMap<FString, FString>& Extensions, const FString& Key, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Extensions.Add(Key, Value);
		}
	}

	void AddNumericExtensionIfPresent(
		const TSharedPtr<FJsonObject>& Obj,
		TMap<FString, FString>& Extensions,
		const FString& SourceKey,
		const FString& ExtKey)
	{
		double Value = 0.0;
		if (TryGetNumber(Obj, SourceKey, Value))
		{
			Extensions.Add(ExtKey, FString::SanitizeFloat(static_cast<float>(Value)));
		}
	}

	bool IsSemanticFieldName(const FString& FieldName)
	{
		return FieldName.Equals(TEXT("placeholder"), ESearchCase::IgnoreCase)
			|| FieldName.Equals(TEXT("slot"), ESearchCase::IgnoreCase);
	}

	bool IsExplicitFalseString(const FString& InValue)
	{
		const FString Value = InValue.TrimStartAndEnd().ToLower();
		return Value.IsEmpty()
			|| Value == TEXT("false")
			|| Value == TEXT("0")
			|| Value == TEXT("no")
			|| Value == TEXT("off")
			|| Value == TEXT("none")
			|| Value == TEXT("null");
	}

	bool ReadSemanticSignalField(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		switch (Value->Type)
		{
		case EJson::Boolean:
			return Value->AsBool();
		case EJson::Number:
			return !FMath::IsNearlyZero(static_cast<float>(Value->AsNumber()));
		case EJson::String:
			return !IsExplicitFalseString(Value->AsString());
		case EJson::Array:
		case EJson::Object:
			return true;
		default:
			return false;
		}
	}

	// Depth cap for scanning untrusted pluginData/sharedPluginData JSON for semantic
	// hints. This recursion runs outside the parser's FDesignParserBudget, so it carries
	// its own bound, styled after DesignTreeLimits' named limits; real pluginData values
	// nest only a handful of levels, so 32 is a generous fixed ceiling, not a measured
	// optimum.
	constexpr int32 MaxPluginDataHintScanDepth = 32;

	bool TryReadSemanticHintFromJsonValue(const TSharedPtr<FJsonValue>& Value, int32 Depth = 0)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		if (Depth >= MaxPluginDataHintScanDepth)
		{
			UE_LOG(LogDesign2UMG, Verbose,
				TEXT("FigmaParserMetadata: pluginData semantic-hint scan stopped at depth cap (%d)"),
				MaxPluginDataHintScanDepth);
			return false;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> ObjectValue = Value->AsObject();
			if (!ObjectValue.IsValid())
			{
				return false;
			}

			for (const auto& Pair : ObjectValue->Values)
			{
				if (IsSemanticFieldName(FString(*Pair.Key)) && ReadSemanticSignalField(Pair.Value))
				{
					return true;
				}
				if (TryReadSemanticHintFromJsonValue(Pair.Value, Depth + 1))
				{
					return true;
				}
			}
			return false;
		}

		if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& ChildValue : Value->AsArray())
			{
				if (TryReadSemanticHintFromJsonValue(ChildValue, Depth + 1))
				{
					return true;
				}
			}
			return false;
		}

		if (Value->Type == EJson::String)
		{
			const FString StringValue = Value->AsString().TrimStartAndEnd();
			if (StringValue.IsEmpty())
			{
				return false;
			}

			TSharedPtr<FJsonObject> JsonObject;
			if (DesignJsonUtils::TryParseObject(StringValue, JsonObject))
			{
				return TryReadSemanticHintFromJsonValue(MakeShared<FJsonValueObject>(JsonObject), Depth + 1);
			}

			return IsSemanticFieldName(StringValue);
		}

		return false;
	}

	bool HasVisiblePaint(const FDesignFill& Fill)
	{
		if (!Fill.bEnabled || Fill.IsNone() || Fill.Opacity <= 0) return false;
		if (Fill.IsColor()) return FUMGValueParser::ParseColor(Fill.SolidColor, Fill.Opacity).A > 0;
		if (Fill.Type == EDesignFillType::Gradient)
		{
			return Fill.Gradient.Colors.ContainsByPredicate([](const FDesignGradientStop& Stop)
			{
				return FUMGValueParser::ParseColor(Stop.Color).A > 0;
			});
		}
		return true;
	}

	FBox2D ReadSourceBounds(const FDesignNode& Node, const TCHAR* Field)
	{
		FigmaParserInternal::FNodeBounds Bounds;
		if (!FigmaParserInternal::TryReadBounds(DesignJsonUtils::GetObject(Node.Source().Prototype.AnimationSource, Field), Bounds))
		{
			return FBox2D(ForceInit);
		}
		return FBox2D(FVector2D(Bounds.X, Bounds.Y), FVector2D(Bounds.X + Bounds.W, Bounds.Y + Bounds.H));
	}

	FBox2D IntersectPaintBounds(const FBox2D& Paint, const FBox2D& Clip)
	{
		if (!Paint.bIsValid || !Clip.bIsValid || !Paint.Intersect(Clip)) return FBox2D(ForceInit);
		return FBox2D(FVector2D(FMath::Max(Paint.Min.X, Clip.Min.X), FMath::Max(Paint.Min.Y, Clip.Min.Y)),
			FVector2D(FMath::Min(Paint.Max.X, Clip.Max.X), FMath::Min(Paint.Max.Y, Clip.Max.Y)));
	}

	FBox2D ResolvePaintBounds(const FDesignNode& Node, const FVector2D& ParentX, const FVector2D& ParentY)
	{
		if (!Node.Transform.bEnabled || Node.Transform.Opacity <= 0) return FBox2D(ForceInit);
		const FVector2D LocalX = FVector2D(Node.Transform.bFlipX ? -1 : 1, 0).GetRotated(Node.Transform.Rotation);
		const FVector2D LocalY = FVector2D(0, Node.Transform.bFlipY ? -1 : 1).GetRotated(Node.Transform.Rotation);
		const FVector2D AxisX = ParentX * LocalX.X + ParentY * LocalX.Y;
		const FVector2D AxisY = ParentX * LocalY.X + ParentY * LocalY.Y;
		const FBox2D Layout = ReadSourceBounds(Node, TEXT("absoluteBoundingBox"));
		const bool bFill = Node.Visual.Fills.ContainsByPredicate(HasVisiblePaint);
		const FDesignStroke& Stroke = Node.Visual.Stroke;
		const bool bStroke = Stroke.HasStroke() && Stroke.Fill.IsSet() && HasVisiblePaint(Stroke.Fill.GetValue());
		const bool bEffectPaint = Node.Visual.Effects.ContainsByPredicate([](const FDesignEffect& Effect)
		{
			return Effect.bEnabled && ((Effect.Type == EDesignEffectType::Shadow
				&& FUMGValueParser::ParseColor(Effect.Shadow.Color).A > 0)
				|| (Effect.Type == EDesignEffectType::BackgroundBlur && Effect.Radius > 0));
		});
		FBox2D Bounds(ForceInit);
		if ((bFill || bStroke || bEffectPaint) && Layout.bIsValid)
		{
			Bounds = Layout;
			if (bStroke && Stroke.Align != EDesignStrokeAlign::Inside)
			{
				FMargin Insets = VisualMapperStrokeSupport::GetStrokeHalfInsets(Stroke.Thickness);
				if (Stroke.Align == EDesignStrokeAlign::Outside) Insets = Insets * 2.0f;
				FBox2D Padding(ForceInit);
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Padding += AxisX * ((Corner & 1) ? Insets.Right : -Insets.Left)
						+ AxisY * ((Corner & 2) ? Insets.Bottom : -Insets.Top);
				}
				Bounds = FBox2D(Layout.Min + Padding.Min, Layout.Max + Padding.Max);
			}
		}

		// Leaf render bounds include source glyph/vector geometry, stroke and effects.
		// Container render bounds also contain layout aggregates, so they cannot identify its own paint.
		if (!Node.IsContainer())
		{
			const FBox2D Render = ReadSourceBounds(Node, TEXT("absoluteRenderBounds"));
			if ((bFill || bStroke || bEffectPaint) && Render.bIsValid)
			{
				return Render;
			}
		}
		for (const TSharedPtr<FDesignNode>& Child : Node.Children)
		{
			FBox2D ChildBounds = ResolvePaintBounds(*Child, AxisX, AxisY);
			if (Node.LayoutData.ExplicitClip.Get(false) || Node.Visual.GeometryMask.HasMask())
			{
				ChildBounds = IntersectPaintBounds(ChildBounds, Layout);
			}
			Bounds += ChildBounds;
		}
		if (!Bounds.bIsValid) return Bounds;
		const FBox2D Content = Bounds;
		double BlurVariance = 0;
		for (const FDesignEffect& Effect : Node.Visual.Effects)
		{
			if (!Effect.bEnabled) continue;
			if (Effect.Type == EDesignEffectType::Shadow && !Effect.Shadow.bInner
				&& FUMGValueParser::ParseColor(Effect.Shadow.Color).A > 0)
			{
				// Match the existing native effect extent: three sigma plus spread.
				const FVector2D Radius(Effect.Shadow.Blur * 1.5 + FMath::Max(0.0f, Effect.Shadow.Spread));
				const FVector2D Offset = AxisX * Effect.Shadow.OffsetX + AxisY * Effect.Shadow.OffsetY;
				Bounds += FBox2D(Content.Min + Offset - Radius, Content.Max + Offset + Radius);
			}
			else if (Effect.Type == EDesignEffectType::Blur) BlurVariance += FMath::Square(Effect.Radius * 0.5);
		}
		if (BlurVariance > 0)
		{
			const FVector2D Radius(3 * FMath::Sqrt(BlurVariance));
			Bounds = FBox2D(Bounds.Min - Radius, Bounds.Max + Radius);
		}
		return Bounds;
	}
}

namespace FigmaParserMetadata
{
	FBox2D CalculateExportBounds(const FDesignNode& Root, const FVector2D& SourceOrigin)
	{
		FBox2D Result(FVector2D::ZeroVector, FVector2D(Root.Transform.Width.Value, Root.Transform.Height.Value));
		const FBox2D Paint = ResolvePaintBounds(Root, FVector2D(1, 0), FVector2D(0, 1));
		// Figma exports whole pixels; overflow under half a pixel does not widen the export.
		const auto Snap = [](const FVector2D& Point) { return FVector2D(FMath::RoundToDouble(Point.X), FMath::RoundToDouble(Point.Y)); };
		if (Paint.bIsValid) Result += FBox2D(Snap(Paint.Min - SourceOrigin), Snap(Paint.Max - SourceOrigin));
		return Result;
	}

	void CaptureSharedMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
	{
		if (!Obj.IsValid())
		{
			return;
		}

		for (const TCHAR* Key : { TEXT("overflowDirection"), TEXT("prototypeStartNodeID"), TEXT("transitionNodeID") })
		{
			AddExtensionIfNonEmpty(Extensions, Key, GetString(Obj, Key));
		}

		for (const TCHAR* Key : {
			 TEXT("layoutGrids"), TEXT("interactions"), TEXT("flowStartingPoints"), TEXT("prototypeDevice"),
			 TEXT("componentPropertyReferences"), TEXT("explicitVariableModes"), TEXT("resolvedVariableModes"),
			 TEXT("componentPropertyDefinitions"), TEXT("variantGroupProperties"), TEXT("annotations"), TEXT("measurements"),
			 TEXT("exportSettings"), TEXT("devStatus"), TEXT("branchData") })
		{
			FigmaParserInternal::AddJsonExtensionIfPresent(Obj, Extensions, Key, Key);
		}
	}

	void CaptureLayoutMetadataExtensions(const TSharedPtr<FJsonObject>& Obj, TMap<FString, FString>& Extensions)
	{
		if (!Obj.IsValid())
		{
			return;
		}

		for (const TCHAR* Key : {
			 TEXT("counterAxisAlignContent"), TEXT("layoutPositioning"), TEXT("layoutWrap"), TEXT("layoutAlign"),
			 TEXT("primaryAxisSizingMode"), TEXT("counterAxisSizingMode"), TEXT("gridRowsSizing"), TEXT("gridColumnsSizing"),
			 TEXT("gridChildHorizontalAlign"), TEXT("gridChildVerticalAlign"), TEXT("mainComponentKey"), TEXT("componentSetId"), TEXT("componentSetKey") })
		{
			AddExtensionIfNonEmpty(Extensions, Key, GetString(Obj, Key));
		}

		for (const TCHAR* Key : {
			 TEXT("counterAxisSpacing"), TEXT("minWidth"), TEXT("maxWidth"), TEXT("minHeight"),
			 TEXT("maxHeight"), TEXT("gridRowCount"), TEXT("gridColumnCount"), TEXT("gridRowGap"), TEXT("gridColumnGap"),
			 TEXT("gridRowSpan"), TEXT("gridColumnSpan"), TEXT("gridColumnAnchorIndex"), TEXT("gridRowAnchorIndex") })
		{
			AddNumericExtensionIfPresent(Obj, Extensions, Key, Key);
		}

		for (const TCHAR* Key : { TEXT("componentProperties"), TEXT("variantProperties"), TEXT("overrides"), TEXT("exposedInstances") })
		{
			FigmaParserInternal::AddJsonExtensionIfPresent(Obj, Extensions, Key, Key);
		}

		CaptureSharedMetadataExtensions(Obj, Extensions);
	}

	void ApplyFigmaSemanticHints(const TSharedPtr<FJsonObject>& NodeObj, FDesignNode& Node)
	{
		if (!NodeObj.IsValid())
		{
			return;
		}
		const TSharedPtr<FJsonValue> PlaceholderField = NodeObj->TryGetField(TEXT("placeholder"));
		const TSharedPtr<FJsonValue> SlotField = NodeObj->TryGetField(TEXT("slot"));

		if (SlotField.IsValid() && SlotField->Type == EJson::Array)
		{
			ParserSharedSemantics::AddSourceSlotMetadata(Node, SlotField->AsArray().Num());
		}

		const bool bExplicitPlaceholder = ReadSemanticSignalField(PlaceholderField);
		const bool bExplicitSlot = ReadSemanticSignalField(SlotField);
		const bool bHasPluginSemanticMarker =
			TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("pluginData")))
			|| TryReadSemanticHintFromJsonValue(NodeObj->TryGetField(TEXT("sharedPluginData")));

		// Placeholder surface is the main runtime contract for this family; present/count stay preservation-only metadata.
		Node.LayoutData.bSemanticPlaceholderSurface =
			bExplicitPlaceholder
			|| bExplicitSlot
			|| bHasPluginSemanticMarker;
	}
}
