// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Automation/Design2UMGAutomationIrDump.h"

#include "Design2UMGLog.h"
#include "UMGSchema.h"
#include "UMGSchemaFoundation.h"
#include "UMGSchemaSourceExtensions.h"

#include "Design2UMGAutomationDumpFormat.h"
#include "HAL/IConsoleManager.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"

using Design2UMGAutomation::RenderFloat;

namespace
{
	TAutoConsoleVariable<bool> CVarDesign2UMGAutomationIrDump(
		TEXT("design2umgAutomation.IrDump"),
		false,
		TEXT("Enables the IR golden dump (ir_dump.json) of the converted FUMGWidgetData typed tree during automation renders. Also requires a non-empty automation dump path in the render spec."),
		ECVF_Default);

	// ---------------------------------------------------------------------------
	// Primitive rendering. Every float uses %.9g (float32 round-trip lossless);
	// hex strings and %.1f/%.6f are forbidden by the IR golden layer contract.
	// ---------------------------------------------------------------------------

	FString RenderInt(const int32 Value)
	{
		return FString::Printf(TEXT("%d"), Value);
	}

	FString RenderBool(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString RenderString(const FString& Value)
	{
		return FString(TEXT("\"")) + Design2UMGAutomation::EscapeJsonString(Value) + TEXT("\"");
	}

	// ---------------------------------------------------------------------------
	// Object rendering with byte-stable key order: every object collects its
	// (key, rendered value) pairs and sorts them by key, so no container
	// iteration order can leak into the output.
	// ---------------------------------------------------------------------------

	using FJsonField = TPair<FString, FString>;
	using FJsonFields = TArray<FJsonField>;

	FString RenderObject(const FJsonFields& Fields)
	{
		FJsonFields Sorted = Fields;
		Sorted.Sort([](const FJsonField& A, const FJsonField& B) { return A.Key < B.Key; });
		FString Out(TEXT("{"));
		for (int32 Index = 0; Index < Sorted.Num(); ++Index)
		{
			if (Index > 0)
			{
				Out += TEXT(",");
			}
			Out += RenderString(Sorted[Index].Key);
			Out += TEXT(":");
			Out += Sorted[Index].Value;
		}
		Out += TEXT("}");
		return Out;
	}

	FString RenderArray(const TArray<FString>& Items)
	{
		FString Out(TEXT("["));
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (Index > 0)
			{
				Out += TEXT(",");
			}
			Out += Items[Index];
		}
		Out += TEXT("]");
		return Out;
	}

	template <typename T>
	FString RenderObjectArray(const TArray<T>& Items, const TFunction<FString(const T&)>& RenderItem)
	{
		TArray<FString> Rendered;
		Rendered.Reserve(Items.Num());
		for (const T& Item : Items)
		{
			Rendered.Add(RenderItem(Item));
		}
		return RenderArray(Rendered);
	}

	void AddString(FJsonFields& Fields, const TCHAR* Key, const FString& Value)
	{
		Fields.Add(FJsonField(Key, RenderString(Value)));
	}

	void AddBool(FJsonFields& Fields, const TCHAR* Key, const bool bValue)
	{
		Fields.Add(FJsonField(Key, RenderBool(bValue)));
	}

	void AddInt(FJsonFields& Fields, const TCHAR* Key, const int32 Value)
	{
		Fields.Add(FJsonField(Key, RenderInt(Value)));
	}

	void AddFloat(FJsonFields& Fields, const TCHAR* Key, const float Value)
	{
		Fields.Add(FJsonField(Key, RenderFloat(Value)));
	}

	void AddOptionalString(FJsonFields& Fields, const TCHAR* Key, const TOptional<FString>& Value)
	{
		if (Value.IsSet())
		{
			AddString(Fields, Key, Value.GetValue());
		}
	}

	void AddOptionalBool(FJsonFields& Fields, const TCHAR* Key, const TOptional<bool>& Value)
	{
		if (Value.IsSet())
		{
			AddBool(Fields, Key, Value.GetValue());
		}
	}

	void AddOptionalInt(FJsonFields& Fields, const TCHAR* Key, const TOptional<int32>& Value)
	{
		if (Value.IsSet())
		{
			AddInt(Fields, Key, Value.GetValue());
		}
	}

	void AddOptionalFloat(FJsonFields& Fields, const TCHAR* Key, const TOptional<float>& Value)
	{
		if (Value.IsSet())
		{
			AddFloat(Fields, Key, Value.GetValue());
		}
	}

	// ---------------------------------------------------------------------------
	// Math / color value objects.
	// ---------------------------------------------------------------------------

	FString RenderMargin(const FMargin& Value)
	{
		FJsonFields Fields;
		AddFloat(Fields, TEXT("Left"), Value.Left);
		AddFloat(Fields, TEXT("Top"), Value.Top);
		AddFloat(Fields, TEXT("Right"), Value.Right);
		AddFloat(Fields, TEXT("Bottom"), Value.Bottom);
		return RenderObject(Fields);
	}

	void AddOptionalMargin(FJsonFields& Fields, const TCHAR* Key, const TOptional<FMargin>& Value)
	{
		if (Value.IsSet())
		{
			Fields.Add(FJsonField(Key, RenderMargin(Value.GetValue())));
		}
	}

	FString RenderVector2D(const FVector2D& Value)
	{
		FJsonFields Fields;
		AddFloat(Fields, TEXT("X"), Value.X);
		AddFloat(Fields, TEXT("Y"), Value.Y);
		return RenderObject(Fields);
	}

	void AddOptionalVector2D(FJsonFields& Fields, const TCHAR* Key, const TOptional<FVector2D>& Value)
	{
		if (Value.IsSet())
		{
			Fields.Add(FJsonField(Key, RenderVector2D(Value.GetValue())));
		}
	}

	FString RenderVector4(const FVector4& Value)
	{
		FJsonFields Fields;
		AddFloat(Fields, TEXT("X"), Value.X);
		AddFloat(Fields, TEXT("Y"), Value.Y);
		AddFloat(Fields, TEXT("Z"), Value.Z);
		AddFloat(Fields, TEXT("W"), Value.W);
		return RenderObject(Fields);
	}

	void AddOptionalVector4(FJsonFields& Fields, const TCHAR* Key, const TOptional<FVector4>& Value)
	{
		if (Value.IsSet())
		{
			Fields.Add(FJsonField(Key, RenderVector4(Value.GetValue())));
		}
	}

	FString RenderColor(const FLinearColor& Value)
	{
		FJsonFields Fields;
		AddFloat(Fields, TEXT("R"), Value.R);
		AddFloat(Fields, TEXT("G"), Value.G);
		AddFloat(Fields, TEXT("B"), Value.B);
		AddFloat(Fields, TEXT("A"), Value.A);
		return RenderObject(Fields);
	}

	void AddOptionalColor(FJsonFields& Fields, const TCHAR* Key, const TOptional<FLinearColor>& Value)
	{
		if (Value.IsSet())
		{
			Fields.Add(FJsonField(Key, RenderColor(Value.GetValue())));
		}
	}

	// ---------------------------------------------------------------------------
	// Enum names (stable literals; literal drift breaks the golden on purpose).
	// ---------------------------------------------------------------------------

	const TCHAR* SlotHorizontalAlignmentName(const EUMGSlotHorizontalAlignment Value)
	{
		switch (Value)
		{
		case EUMGSlotHorizontalAlignment::Fill: return TEXT("Fill");
		case EUMGSlotHorizontalAlignment::Left: return TEXT("Left");
		case EUMGSlotHorizontalAlignment::Center: return TEXT("Center");
		case EUMGSlotHorizontalAlignment::Right: return TEXT("Right");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* SlotVerticalAlignmentName(const EUMGSlotVerticalAlignment Value)
	{
		switch (Value)
		{
		case EUMGSlotVerticalAlignment::Fill: return TEXT("Fill");
		case EUMGSlotVerticalAlignment::Top: return TEXT("Top");
		case EUMGSlotVerticalAlignment::Center: return TEXT("Center");
		case EUMGSlotVerticalAlignment::Bottom: return TEXT("Bottom");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* SlotSizeRuleName(const EUMGSlotSizeRule Value)
	{
		switch (Value)
		{
		case EUMGSlotSizeRule::Auto: return TEXT("Auto");
		case EUMGSlotSizeRule::Fill: return TEXT("Fill");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* BrushDrawTypeName(const ESlateBrushDrawType::Type Value)
	{
		switch (Value)
		{
		case ESlateBrushDrawType::NoDrawType: return TEXT("NoDrawType");
		case ESlateBrushDrawType::Box: return TEXT("Box");
		case ESlateBrushDrawType::Border: return TEXT("Border");
		case ESlateBrushDrawType::Image: return TEXT("Image");
		case ESlateBrushDrawType::RoundedBox: return TEXT("RoundedBox");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* GeometryMaskShapeName(const EUMGGeometryMaskShape Value)
	{
		switch (Value)
		{
		case EUMGGeometryMaskShape::Ellipse: return TEXT("Ellipse");
		case EUMGGeometryMaskShape::RoundedRectangle: return TEXT("RoundedRectangle");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* PrototypeNavigationActionName(const EUMGPrototypeNavigationAction Value)
	{
		switch (Value)
		{
		case EUMGPrototypeNavigationAction::Navigate: return TEXT("Navigate");
		case EUMGPrototypeNavigationAction::Overlay: return TEXT("Overlay");
		case EUMGPrototypeNavigationAction::Back: return TEXT("Back");
		case EUMGPrototypeNavigationAction::OpenLink: return TEXT("OpenLink");
		case EUMGPrototypeNavigationAction::SetVariable: return TEXT("SetVariable");
		case EUMGPrototypeNavigationAction::SetVariableMode: return TEXT("SetVariableMode");
		case EUMGPrototypeNavigationAction::Conditional: return TEXT("Conditional");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* PrototypeNavigationStepKindName(const EUMGPrototypeNavigationStepKind Value)
	{
		switch (Value)
		{
		case EUMGPrototypeNavigationStepKind::Navigate: return TEXT("Navigate");
		case EUMGPrototypeNavigationStepKind::Overlay: return TEXT("Overlay");
		case EUMGPrototypeNavigationStepKind::Back: return TEXT("Back");
		case EUMGPrototypeNavigationStepKind::OpenLink: return TEXT("OpenLink");
		case EUMGPrototypeNavigationStepKind::SetVariable: return TEXT("SetVariable");
		case EUMGPrototypeNavigationStepKind::SetVariableMode: return TEXT("SetVariableMode");
		case EUMGPrototypeNavigationStepKind::ConditionJump: return TEXT("ConditionJump");
		case EUMGPrototypeNavigationStepKind::Jump: return TEXT("Jump");
		default: return TEXT("Unknown");
		}
	}

	// ---------------------------------------------------------------------------
	// Typed payload visitors. Coverage follows UMGSchema.h / UMGSchemaTypedPayloads.h
	// / UMGSchemaFoundation.h / UMGSchemaSourceExtensions.h member by member.
	// Structs exposing HasAny() are emitted only when set; plain-value structs
	// (TextStyle, Brush, Slot, Bounds) are always emitted with their defaults.
	// ---------------------------------------------------------------------------

	FString RenderSlotData(const FUMGSlotData& Slot)
	{
		FJsonFields Anchors;
		{
			FJsonFields Min;
			AddFloat(Min, TEXT("X"), Slot.Anchors.Min.X);
			AddFloat(Min, TEXT("Y"), Slot.Anchors.Min.Y);
			FJsonFields Max;
			AddFloat(Max, TEXT("X"), Slot.Anchors.Max.X);
			AddFloat(Max, TEXT("Y"), Slot.Anchors.Max.Y);
			Anchors.Add(FJsonField(TEXT("Max"), RenderObject(Max)));
			Anchors.Add(FJsonField(TEXT("Min"), RenderObject(Min)));
		}

		FJsonFields Alignment;
		AddFloat(Alignment, TEXT("X"), Slot.Alignment.X);
		AddFloat(Alignment, TEXT("Y"), Slot.Alignment.Y);

		FJsonFields Offsets;
		AddFloat(Offsets, TEXT("Bottom"), Slot.Offsets.Bottom);
		AddFloat(Offsets, TEXT("Left"), Slot.Offsets.Left);
		AddFloat(Offsets, TEXT("Right"), Slot.Offsets.Right);
		AddFloat(Offsets, TEXT("Top"), Slot.Offsets.Top);

		FJsonFields Fields;
		Fields.Add(FJsonField(TEXT("Alignment"), RenderObject(Alignment)));
		Fields.Add(FJsonField(TEXT("Anchors"), RenderObject(Anchors)));
		AddBool(Fields, TEXT("AutoSize"), Slot.bAutoSize);
		AddFloat(Fields, TEXT("FillWeight"), Slot.FillWeight);
		AddString(Fields, TEXT("HorizontalAlignment"), SlotHorizontalAlignmentName(Slot.HorizontalAlignment));
		AddInt(Fields, TEXT("Column"), Slot.Column);
		AddInt(Fields, TEXT("ColumnSpan"), Slot.ColumnSpan);
		Fields.Add(FJsonField(TEXT("Offsets"), RenderObject(Offsets)));
		Fields.Add(FJsonField(TEXT("Padding"), RenderMargin(Slot.Padding)));
		AddInt(Fields, TEXT("Row"), Slot.Row);
		AddInt(Fields, TEXT("RowSpan"), Slot.RowSpan);
		AddString(Fields, TEXT("SizeRule"), SlotSizeRuleName(Slot.SizeRule));
		AddString(Fields, TEXT("VerticalAlignment"), SlotVerticalAlignmentName(Slot.VerticalAlignment));
		return RenderObject(Fields);
	}

	FString RenderTextStyleData(const FUMGTextStyleData& TextStyle)
	{
		FJsonFields Fields;
		AddFloat(Fields, TEXT("FontSize"), TextStyle.FontSize);
		AddFloat(Fields, TEXT("SourceFontSize"), TextStyle.SourceFontSize);
		AddOptionalFloat(Fields, TEXT("LetterSpacing"), TextStyle.LetterSpacing);
		return RenderObject(Fields);
	}

	FString RenderWidgetStyleData(const FUMGWidgetStyleData& WidgetStyle)
	{
		FJsonFields Fields;
		AddOptionalFloat(Fields, TEXT("WidthOverride"), WidgetStyle.WidthOverride);
		AddOptionalFloat(Fields, TEXT("HeightOverride"), WidgetStyle.HeightOverride);
		AddOptionalFloat(Fields, TEXT("MinDesiredWidth"), WidgetStyle.MinDesiredWidth);
		AddOptionalFloat(Fields, TEXT("MinDesiredHeight"), WidgetStyle.MinDesiredHeight);
		AddOptionalMargin(Fields, TEXT("Padding"), WidgetStyle.Padding);
		AddOptionalVector2D(Fields, TEXT("InnerSlotPadding"), WidgetStyle.InnerSlotPadding);
		AddOptionalMargin(Fields, TEXT("SlotPadding"), WidgetStyle.SlotPadding);
		AddOptionalMargin(Fields, TEXT("StrokePadding"), WidgetStyle.StrokePadding);
		AddOptionalMargin(Fields, TEXT("StrokeMargin"), WidgetStyle.StrokeMargin);
		AddOptionalString(Fields, TEXT("ImageMode"), WidgetStyle.ImageMode);
		AddOptionalString(Fields, TEXT("ImageTransform"), WidgetStyle.ImageTransform);
		AddOptionalString(Fields, TEXT("Stretch"), WidgetStyle.Stretch);
		AddOptionalString(Fields, TEXT("StretchDirection"), WidgetStyle.StretchDirection);
		AddOptionalColor(Fields, TEXT("BackgroundColor"), WidgetStyle.BackgroundColor);
		AddOptionalColor(Fields, TEXT("HoveredColor"), WidgetStyle.HoveredColor);
		AddOptionalColor(Fields, TEXT("PressedColor"), WidgetStyle.PressedColor);
		AddOptionalString(Fields, TEXT("NamedSlot"), WidgetStyle.NamedSlot);
		AddOptionalString(Fields, TEXT("SlotName"), WidgetStyle.SlotName);
		AddOptionalString(Fields, TEXT("Visibility"), WidgetStyle.Visibility);
		AddOptionalString(Fields, TEXT("ToolTipText"), WidgetStyle.ToolTipText);
		AddOptionalBool(Fields, TEXT("IsEnabled"), WidgetStyle.IsEnabled);
		return RenderObject(Fields);
	}

	FString RenderTextData(const FUMGTextData& Text)
	{
		FJsonFields Fields;
		AddOptionalString(Fields, TEXT("Text"), Text.Text);
		AddOptionalString(Fields, TEXT("RichTextStyleRowsJson"), Text.RichTextStyleRowsJson);
		AddOptionalString(Fields, TEXT("HintText"), Text.HintText);
		AddOptionalString(Fields, TEXT("FontFamily"), Text.FontFamily);
		AddOptionalString(Fields, TEXT("TypefaceFontName"), Text.TypefaceFontName);
		AddOptionalString(Fields, TEXT("FontStyle"), Text.FontStyle);
		AddOptionalString(Fields, TEXT("Justification"), Text.Justification);
		AddOptionalBool(Fields, TEXT("AutoWrapText"), Text.bAutoWrapText);
		AddOptionalFloat(Fields, TEXT("WrapTextAt"), Text.WrapTextAt);
		AddOptionalFloat(Fields, TEXT("LineHeightPercentage"), Text.LineHeightPercentage);
		AddOptionalBool(Fields, TEXT("Underline"), Text.bUnderline);
		AddOptionalBool(Fields, TEXT("Strikethrough"), Text.bStrikethrough);
		AddOptionalString(Fields, TEXT("Href"), Text.Href);
		AddOptionalString(Fields, TEXT("TextAlignVertical"), Text.TextAlignVertical);
		AddOptionalString(Fields, TEXT("IconName"), Text.IconName);
		AddOptionalColor(Fields, TEXT("Color"), Text.Color);
		return RenderObject(Fields);
	}

	FString RenderImageTransformData(const FUMGVisualBrushData::FImageTransformData& Transform)
	{
		FJsonFields Fields;
		AddFloat(Fields, TEXT("A"), Transform.A);
		AddFloat(Fields, TEXT("B"), Transform.B);
		AddFloat(Fields, TEXT("C"), Transform.C);
		AddFloat(Fields, TEXT("D"), Transform.D);
		AddFloat(Fields, TEXT("TX"), Transform.TX);
		AddFloat(Fields, TEXT("TY"), Transform.TY);
		return RenderObject(Fields);
	}

	FString RenderVisualBrushData(const FUMGVisualBrushData& Brush)
	{
		FJsonFields Fields;
		AddOptionalFloat(Fields, TEXT("OutlineCornerRadius"), Brush.OutlineCornerRadius);
		AddOptionalVector4(Fields, TEXT("CornerRadii"), Brush.CornerRadii);
		AddOptionalFloat(Fields, TEXT("OutlineThickness"), Brush.OutlineThickness);
		AddOptionalMargin(Fields, TEXT("BrushMargin"), Brush.Margin);
		if (Brush.DrawAs.IsSet())
		{
			AddString(Fields, TEXT("DrawAs"), BrushDrawTypeName(Brush.DrawAs.GetValue()));
		}
		AddOptionalColor(Fields, TEXT("BrushColor"), Brush.BrushColor);
		AddOptionalColor(Fields, TEXT("OutlineColor"), Brush.OutlineColor);
		AddOptionalString(Fields, TEXT("MaterialRef"), Brush.MaterialRef);
		AddOptionalString(Fields, TEXT("MaterialParams"), Brush.MaterialParams);
		AddString(Fields, TEXT("TexturePath"), Brush.TexturePath);

		if (Brush.Image.HasAny())
		{
			FJsonFields Image;
			AddOptionalString(Image, TEXT("TexturePath"), Brush.Image.TexturePath);
			AddOptionalString(Image, TEXT("Mode"), Brush.Image.Mode);
			if (Brush.Image.Transform.IsSet())
			{
				Image.Add(FJsonField(TEXT("Transform"), RenderImageTransformData(Brush.Image.Transform.GetValue())));
			}
			Fields.Add(FJsonField(TEXT("Image"), RenderObject(Image)));
		}

		if (Brush.Gradient.HasAny())
		{
			FJsonFields Gradient;
			AddOptionalString(Gradient, TEXT("Type"), Brush.Gradient.Type);
			AddOptionalFloat(Gradient, TEXT("Rotation"), Brush.Gradient.Rotation);
			AddOptionalVector2D(Gradient, TEXT("StartPoint"), Brush.Gradient.StartPoint);
			AddOptionalVector2D(Gradient, TEXT("EndPoint"), Brush.Gradient.EndPoint);
			AddOptionalVector2D(Gradient, TEXT("Center"), Brush.Gradient.Center);
			AddOptionalVector2D(Gradient, TEXT("Size"), Brush.Gradient.Size);
			Gradient.Add(FJsonField(
				TEXT("Stops"),
				RenderObjectArray<FUMGVisualBrushData::FGradientStopData>(
					Brush.Gradient.Stops,
					[](const FUMGVisualBrushData::FGradientStopData& Stop)
					{
						FJsonFields StopFields;
						StopFields.Add(FJsonField(TEXT("Color"), RenderColor(Stop.Color)));
						AddFloat(StopFields, TEXT("Position"), Stop.Position);
						return RenderObject(StopFields);
					})));
			Fields.Add(FJsonField(TEXT("Gradient"), RenderObject(Gradient)));
		}
		return RenderObject(Fields);
	}

	FString RenderEffectData(const FUMGEffectData& Effect)
	{
		FJsonFields Fields;
		AddOptionalFloat(Fields, TEXT("BlurStrength"), Effect.BlurStrength);
		AddOptionalBool(Fields, TEXT("RetainRendering"), Effect.bRetainRendering);
		AddOptionalString(Fields, TEXT("EffectMaterial"), Effect.EffectMaterial);
		AddOptionalString(Fields, TEXT("TextureParameter"), Effect.TextureParameter);
		AddOptionalVector2D(Fields, TEXT("EffectBlurUVStep"), Effect.EffectBlurUVStep);
		AddOptionalColor(Fields, TEXT("EffectColor"), Effect.EffectColor);
		AddOptionalVector2D(Fields, TEXT("EffectOffset"), Effect.EffectOffset);
		if (Effect.GeometryMaskShape.IsSet())
		{
			AddString(Fields, TEXT("GeometryMaskShape"), GeometryMaskShapeName(Effect.GeometryMaskShape.GetValue()));
		}
		AddOptionalVector2D(Fields, TEXT("GeometryMaskSize"), Effect.GeometryMaskSize);
		AddOptionalVector4(Fields, TEXT("GeometryMaskCornerRadii"), Effect.GeometryMaskCornerRadii);
		if (Effect.RenderingPhase.IsSet())
		{
			FJsonFields Phase;
			AddInt(Phase, TEXT("Phase"), Effect.RenderingPhase->Phase);
			AddInt(Phase, TEXT("PhaseCount"), Effect.RenderingPhase->PhaseCount);
			Fields.Add(FJsonField(TEXT("RenderingPhase"), RenderObject(Phase)));
		}
		return RenderObject(Fields);
	}

	FString RenderPathData(const FUMGPathData& Path)
	{
		FJsonFields Fields;
		AddOptionalString(Fields, TEXT("Geometry"), Path.Geometry);
		AddOptionalString(Fields, TEXT("FillRule"), Path.FillRule);
		AddOptionalColor(Fields, TEXT("FillColor"), Path.FillColor);
		AddOptionalColor(Fields, TEXT("StrokeColor"), Path.StrokeColor);
		AddOptionalFloat(Fields, TEXT("StrokeThickness"), Path.StrokeThickness);
		AddOptionalString(Fields, TEXT("StrokeCap"), Path.StrokeCap);
		AddOptionalString(Fields, TEXT("StrokeJoin"), Path.StrokeJoin);
		return RenderObject(Fields);
	}

	FString RenderShadowData(const FUMGShadowData& Shadow)
	{
		FJsonFields Fields;
		AddOptionalVector2D(Fields, TEXT("ShadowOffset"), Shadow.ShadowOffset);
		AddOptionalColor(Fields, TEXT("ShadowColorAndOpacity"), Shadow.ShadowColorAndOpacity);
		AddOptionalFloat(Fields, TEXT("ShadowBlur"), Shadow.ShadowBlur);
		Fields.Add(FJsonField(
			TEXT("BoxShadows"),
			RenderObjectArray<FUMGBoxShadowLayerData>(
				Shadow.BoxShadows,
				[](const FUMGBoxShadowLayerData& Layer)
				{
					FJsonFields LayerFields;
					AddFloat(LayerFields, TEXT("Blur"), Layer.Blur);
					LayerFields.Add(FJsonField(TEXT("Color"), RenderColor(Layer.Color)));
					LayerFields.Add(FJsonField(TEXT("Offset"), RenderVector2D(Layer.Offset)));
					AddFloat(LayerFields, TEXT("Spread"), Layer.Spread);
					return RenderObject(LayerFields);
				})));
		return RenderObject(Fields);
	}

	FString RenderRenderStyleData(const FUMGRenderStyleData& Render)
	{
		FJsonFields Fields;
		AddOptionalFloat(Fields, TEXT("RenderOpacity"), Render.RenderOpacity);
		AddOptionalFloat(Fields, TEXT("RenderAngle"), Render.RenderAngle);
		AddOptionalVector2D(Fields, TEXT("RenderTransformScale"), Render.RenderTransformScale);
		AddOptionalVector2D(Fields, TEXT("RenderTransformPivot"), Render.RenderTransformPivot);
		AddOptionalVector2D(Fields, TEXT("RenderTranslation"), Render.RenderTranslation);
		AddOptionalString(Fields, TEXT("Clipping"), Render.Clipping);
		return RenderObject(Fields);
	}

	FString RenderWidgetStateData(const FUMGWidgetStateData& WidgetState)
	{
		FJsonFields Fields;
		AddOptionalInt(Fields, TEXT("ActiveWidgetIndex"), WidgetState.ActiveWidgetIndex);
		return RenderObject(Fields);
	}

	FString RenderScrollBoxData(const FUMGScrollBoxData& ScrollBox)
	{
		FJsonFields Fields;
		AddOptionalString(Fields, TEXT("Orientation"), ScrollBox.Orientation);
		AddOptionalString(Fields, TEXT("ScrollBarVisibility"), ScrollBox.ScrollBarVisibility);
		return RenderObject(Fields);
	}

	FString RenderSourceNodeData(const FUMGSourceNodeData& Source)
	{
		FJsonFields Fields;
		AddString(Fields, TEXT("SourceNodeId"), Source.SourceNodeId);
		AddString(Fields, TEXT("SourceNodeName"), Source.SourceNodeName);
		AddString(Fields, TEXT("SourceNodeType"), Source.SourceNodeType);
		AddString(Fields, TEXT("ComponentId"), Source.ComponentId);
			AddString(Fields, TEXT("ComponentSetId"), Source.ComponentSetId);
			AddString(Fields, TEXT("ComponentSetKey"), Source.ComponentSetKey);
		AddString(Fields, TEXT("MainComponentKey"), Source.MainComponentKey);
		AddString(Fields, TEXT("ComponentPropertiesJson"), Source.ComponentPropertiesJson);
		AddString(Fields, TEXT("VariantPropertiesJson"), Source.VariantPropertiesJson);
		AddString(Fields, TEXT("OverridesJson"), Source.OverridesJson);
		AddOptionalBool(Fields, TEXT("HasIsExposedInstance"), Source.HasIsExposedInstance);
		AddBool(Fields, TEXT("MappedComponentCandidate"), Source.bMappedComponentCandidate);
		AddString(Fields, TEXT("MappedComponentKey"), Source.MappedComponentKey);
		return RenderObject(Fields);
	}

	FString RenderWidgetNode(const FUMGWidgetNode& Node)
	{
		FJsonFields Fields;
		AddString(Fields, TEXT("Type"), Node.Type);
		AddString(Fields, TEXT("Id"), Node.Id);
		AddString(Fields, TEXT("Name"), Node.Name);
		{
			FJsonFields Bounds;
			AddFloat(Bounds, TEXT("H"), Node.Bounds.H);
			AddFloat(Bounds, TEXT("W"), Node.Bounds.W);
			AddFloat(Bounds, TEXT("X"), Node.Bounds.X);
			AddFloat(Bounds, TEXT("Y"), Node.Bounds.Y);
			Fields.Add(FJsonField(TEXT("Bounds"), RenderObject(Bounds)));
		}
		Fields.Add(FJsonField(TEXT("Slot"), RenderSlotData(Node.Slot)));
		Fields.Add(FJsonField(TEXT("TextStyle"), RenderTextStyleData(Node.TextStyle)));
		if (Node.WidgetStyle.HasAny())
		{
			Fields.Add(FJsonField(TEXT("WidgetStyle"), RenderWidgetStyleData(Node.WidgetStyle)));
		}
		if (Node.Text.HasAny())
		{
			Fields.Add(FJsonField(TEXT("Text"), RenderTextData(Node.Text)));
		}
		Fields.Add(FJsonField(TEXT("Brush"), RenderVisualBrushData(Node.Brush)));
		if (Node.Effect.HasAny())
		{
			Fields.Add(FJsonField(TEXT("Effect"), RenderEffectData(Node.Effect)));
		}
		if (Node.Path.HasAny())
		{
			Fields.Add(FJsonField(TEXT("Path"), RenderPathData(Node.Path)));
		}
		if (Node.Shadow.HasAny())
		{
			Fields.Add(FJsonField(TEXT("Shadow"), RenderShadowData(Node.Shadow)));
		}
		if (Node.Render.HasAny())
		{
			Fields.Add(FJsonField(TEXT("Render"), RenderRenderStyleData(Node.Render)));
		}
		if (Node.WidgetState.HasAny())
		{
			Fields.Add(FJsonField(TEXT("WidgetState"), RenderWidgetStateData(Node.WidgetState)));
		}
		if (Node.ScrollBox.HasAny())
		{
			Fields.Add(FJsonField(TEXT("ScrollBox"), RenderScrollBoxData(Node.ScrollBox)));
		}
		AddBool(Fields, TEXT("SyntheticJustifySpacer"), Node.bSyntheticJustifySpacer);
		if (Node.HasSourceData())
		{
			Fields.Add(FJsonField(TEXT("Source"), RenderSourceNodeData(Node.Source())));
		}
		Fields.Add(FJsonField(
			TEXT("Children"),
			RenderObjectArray<FUMGWidgetNode>(Node.Children, [](const FUMGWidgetNode& Child) { return RenderWidgetNode(Child); })));
		return RenderObject(Fields);
	}

	FString RenderTransitionData(const FUMGPrototypeTransitionData& Transition)
	{
		FJsonFields Fields;
		AddString(Fields, TEXT("Type"), Transition.Type);
		AddString(Fields, TEXT("Direction"), Transition.Direction);
		AddFloat(Fields, TEXT("DurationSeconds"), Transition.DurationSeconds);
		AddString(Fields, TEXT("EasingType"), Transition.EasingType);
		AddString(Fields, TEXT("EasingJson"), Transition.EasingJson);
		AddOptionalBool(Fields, TEXT("HasMatchLayers"), Transition.HasMatchLayers);
		AddString(Fields, TEXT("OutgoingAnimationName"), Transition.OutgoingAnimationName);
		AddString(Fields, TEXT("IncomingAnimationName"), Transition.IncomingAnimationName);
		return RenderObject(Fields);
	}

	FString RenderNavigationStep(const FUMGPrototypeNavigationStep& Step)
	{
		FJsonFields Fields;
		AddString(Fields, TEXT("Kind"), PrototypeNavigationStepKindName(Step.Kind));
		AddString(Fields, TEXT("TargetPageId"), Step.TargetPageId);
		AddInt(Fields, TEXT("TargetPageIndex"), Step.TargetPageIndex);
		AddString(Fields, TEXT("ExternalUrl"), Step.ExternalUrl);
		Fields.Add(FJsonField(TEXT("Transition"), RenderTransitionData(Step.Transition)));
		AddString(Fields, TEXT("VariableId"), Step.VariableId);
		AddString(Fields, TEXT("VariableValueJson"), Step.VariableValueJson);
		AddString(Fields, TEXT("VariableCollectionId"), Step.VariableCollectionId);
		AddString(Fields, TEXT("VariableModeId"), Step.VariableModeId);
		AddString(Fields, TEXT("ConditionJson"), Step.ConditionJson);
		AddInt(Fields, TEXT("JumpTargetIndex"), Step.JumpTargetIndex);
		return RenderObject(Fields);
	}

	FString RenderNavigationBinding(const FUMGPrototypeNavigationBinding& Binding)
	{
		FJsonFields Fields;
		AddString(Fields, TEXT("TriggerWidgetId"), Binding.TriggerWidgetId);
		AddString(Fields, TEXT("TriggerWidgetName"), Binding.TriggerWidgetName);
		AddString(Fields, TEXT("TriggerType"), Binding.TriggerType);
		AddString(Fields, TEXT("TriggerJson"), Binding.TriggerJson);
		{
			TArray<FString> KeyCodes;
			KeyCodes.Reserve(Binding.TriggerKeyCodes.Num());
			for (const FString& Code : Binding.TriggerKeyCodes)
			{
				KeyCodes.Add(RenderString(Code));
			}
			Fields.Add(FJsonField(TEXT("TriggerKeyCodes"), RenderArray(KeyCodes)));
		}
		AddString(Fields, TEXT("TargetPageId"), Binding.TargetPageId);
		AddInt(Fields, TEXT("TargetPageIndex"), Binding.TargetPageIndex);
		AddString(Fields, TEXT("Action"), PrototypeNavigationActionName(Binding.Action));
		AddString(Fields, TEXT("ExternalUrl"), Binding.ExternalUrl);
		Fields.Add(FJsonField(TEXT("Transition"), RenderTransitionData(Binding.Transition)));
		Fields.Add(FJsonField(
			TEXT("Steps"),
			RenderObjectArray<FUMGPrototypeNavigationStep>(
				Binding.Steps,
				[](const FUMGPrototypeNavigationStep& Step) { return RenderNavigationStep(Step); })));
		return RenderObject(Fields);
	}

	FString RenderPrototypeNavigationData(const FUMGPrototypeNavigationData& Navigation)
	{
		FJsonFields Fields;
		AddString(Fields, TEXT("SwitcherWidgetId"), Navigation.SwitcherWidgetId);
		AddString(Fields, TEXT("SwitcherWidgetName"), Navigation.SwitcherWidgetName);
		AddString(Fields, TEXT("OverlayHostWidgetId"), Navigation.OverlayHostWidgetId);
		AddString(Fields, TEXT("OverlayHostWidgetName"), Navigation.OverlayHostWidgetName);
		AddInt(Fields, TEXT("InitialPageIndex"), Navigation.InitialPageIndex);
		{
			TArray<FString> PageIds;
			PageIds.Reserve(Navigation.PageIds.Num());
			for (const FString& PageId : Navigation.PageIds)
			{
				PageIds.Add(RenderString(PageId));
			}
			Fields.Add(FJsonField(TEXT("PageIds"), RenderArray(PageIds)));
		}
		Fields.Add(FJsonField(
			TEXT("ClickBindings"),
			RenderObjectArray<FUMGPrototypeNavigationBinding>(
				Navigation.ClickBindings,
				[](const FUMGPrototypeNavigationBinding& Binding) { return RenderNavigationBinding(Binding); })));
		return RenderObject(Fields);
	}

	FString RenderSourceDocumentData(const FUMGSourceDocumentData& SourceDocument)
	{
		FJsonFields Fields;
		AddString(Fields, TEXT("ComponentsJson"), SourceDocument.ComponentsJson);
		AddString(Fields, TEXT("ComponentSetsJson"), SourceDocument.ComponentSetsJson);
		AddString(Fields, TEXT("Provider"), SourceDocument.Provider);
		AddString(Fields, TEXT("DocumentScope"), SourceDocument.DocumentScope);
		AddString(Fields, TEXT("DocumentScopeOrigin"), SourceDocument.DocumentScopeOrigin);
		return RenderObject(Fields);
	}

	FString RenderIconFontMappingChanges(const FUMGIconFontMappingChanges& Changes)
	{
		FJsonFields Fields;
	if (Changes.Upserts.Num() > 0)
	{
		FJsonFields Upserts;
		Upserts.Reserve(Changes.Upserts.Num());
		for (const TPair<FString, FString>& Pair : Changes.Upserts)
		{
			Upserts.Add(FJsonField(Pair.Key, RenderString(Pair.Value)));
		}
		// RenderObject sorts by key, so TMap iteration order can never leak into the dump bytes.
		Fields.Add(FJsonField(TEXT("Upserts"), RenderObject(Upserts)));
	}
		if (Changes.Removals.Num() > 0)
		{
			TArray<FString> SortedRemovals;
			for (const FString& Removal : Changes.Removals)
			{
				SortedRemovals.Add(Removal);
			}
			// Sort so TSet iteration order can never leak into the dump bytes.
			SortedRemovals.Sort();
			TArray<FString> Rendered;
			Rendered.Reserve(SortedRemovals.Num());
			for (const FString& Removal : SortedRemovals)
			{
				Rendered.Add(RenderString(Removal));
			}
			Fields.Add(FJsonField(TEXT("Removals"), RenderArray(Rendered)));
		}
		return RenderObject(Fields);
	}

	FString RenderWidgetData(const FUMGWidgetData& Data)
	{
		FJsonFields Fields;
		AddString(Fields, TEXT("WidgetName"), Data.WidgetName);
		Fields.Add(FJsonField(TEXT("RootWidget"), RenderWidgetNode(Data.RootWidget)));
		{
			TArray<FString> Warnings;
			Warnings.Reserve(Data.Warnings.Num());
			for (const FString& Warning : Data.Warnings)
			{
				Warnings.Add(RenderString(Warning));
			}
			Fields.Add(FJsonField(TEXT("Warnings"), RenderArray(Warnings)));
		}
		if (Data.IconFontMappingChanges.HasChanges())
		{
			Fields.Add(FJsonField(TEXT("IconFontMappingChanges"), RenderIconFontMappingChanges(Data.IconFontMappingChanges)));
		}
		if (Data.HasSourceDocumentData())
		{
			Fields.Add(FJsonField(TEXT("SourceDocument"), RenderSourceDocumentData(Data.SourceDocument())));
		}
		if (Data.HasPrototypeNavigationData())
		{
			Fields.Add(FJsonField(TEXT("PrototypeNavigation"), RenderPrototypeNavigationData(Data.PrototypeNavigation())));
		}
		return RenderObject(Fields);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
FString Design2UMGAutomationIrDump::GDesign2UMGAutomationIrDumpPath;
#endif

void Design2UMGAutomationIrDump::MaybeWriteIrDump(const FUMGWidgetData& Data)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (!CVarDesign2UMGAutomationIrDump.GetValueOnGameThread() || GDesign2UMGAutomationIrDumpPath.IsEmpty())
	{
		return;
	}

	FString Json = RenderWidgetData(Data);
	Json += TEXT("\n");
	if (FFileHelper::SaveStringToFile(Json, *GDesign2UMGAutomationIrDumpPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogDesign2UMG, Log, TEXT("IrDump: wrote %s"), *GDesign2UMGAutomationIrDumpPath);
	}
	else
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("IrDump: failed to write %s"), *GDesign2UMGAutomationIrDumpPath);
	}
#endif
}
