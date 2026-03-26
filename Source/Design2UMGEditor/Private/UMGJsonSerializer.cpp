// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGJsonSerializer.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeAnchor(const FUMGAnchorData& Anchor)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("X"), Anchor.X);
	J->SetNumberField(TEXT("Y"), Anchor.Y);
	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeAnchors(const FUMGAnchorsData& Anchors)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetObjectField(TEXT("Min"), SerializeAnchor(Anchors.Min));
	J->SetObjectField(TEXT("Max"), SerializeAnchor(Anchors.Max));
	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeOffsets(const FUMGOffsetsData& Offsets)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("Left"), Offsets.Left);
	J->SetNumberField(TEXT("Top"), Offsets.Top);
	J->SetNumberField(TEXT("Right"), Offsets.Right);
	J->SetNumberField(TEXT("Bottom"), Offsets.Bottom);
	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeSlot(const FUMGSlotData& Slot)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetObjectField(TEXT("Anchors"), SerializeAnchors(Slot.Anchors));
	J->SetObjectField(TEXT("Alignment"), SerializeAnchor(Slot.Alignment));
	J->SetObjectField(TEXT("Offsets"), SerializeOffsets(Slot.Offsets));

	const bool bHasPadding =
		!FMath::IsNearlyEqual(Slot.Padding.Left, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Padding.Top, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Padding.Right, 0.0f) ||
		!FMath::IsNearlyEqual(Slot.Padding.Bottom, 0.0f);
	if (bHasPadding)
	{
		J->SetStringField(TEXT("Padding"), FString::Printf(
			TEXT("%.2f,%.2f,%.2f,%.2f"),
			Slot.Padding.Left, Slot.Padding.Top, Slot.Padding.Right, Slot.Padding.Bottom));
	}

	if (Slot.HorizontalAlignment != TEXT("Fill"))
		J->SetStringField(TEXT("HorizontalAlignment"), Slot.HorizontalAlignment);
	if (Slot.VerticalAlignment != TEXT("Fill"))
		J->SetStringField(TEXT("VerticalAlignment"), Slot.VerticalAlignment);
	if (Slot.SizeRule != TEXT("Auto"))
		J->SetStringField(TEXT("SizeRule"), Slot.SizeRule);
	if (!FMath::IsNearlyEqual(Slot.FillWeight, 1.0f))
		J->SetNumberField(TEXT("FillWeight"), Slot.FillWeight);
	if (Slot.Row != 0)
		J->SetNumberField(TEXT("Row"), Slot.Row);
	if (Slot.Column != 0)
		J->SetNumberField(TEXT("Column"), Slot.Column);
	if (Slot.RowSpan != 1)
		J->SetNumberField(TEXT("RowSpan"), Slot.RowSpan);
	if (Slot.ColumnSpan != 1)
		J->SetNumberField(TEXT("ColumnSpan"), Slot.ColumnSpan);
	if (Slot.bAutoSize)
		J->SetBoolField(TEXT("bAutoSize"), true);

	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeBounds(const FBounds& Bounds)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetNumberField(TEXT("X"), Bounds.X);
	J->SetNumberField(TEXT("Y"), Bounds.Y);
	J->SetNumberField(TEXT("W"), Bounds.W);
	J->SetNumberField(TEXT("H"), Bounds.H);
	return J;
}

TSharedRef<FJsonObject> FUMGJsonSerializer::SerializeWidgetNode(const FUMGWidgetNode& Node)
{
	TSharedRef<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("Type"), Node.Type);
	if (!Node.Id.IsEmpty())
		J->SetStringField(TEXT("Id"), Node.Id);
	J->SetStringField(TEXT("Name"), Node.Name);

	const bool bHasBounds =
		!FMath::IsNearlyZero(Node.Bounds.X) || !FMath::IsNearlyZero(Node.Bounds.Y) ||
		!FMath::IsNearlyZero(Node.Bounds.W) || !FMath::IsNearlyZero(Node.Bounds.H);
	if (bHasBounds)
		J->SetObjectField(TEXT("Bounds"), SerializeBounds(Node.Bounds));

	J->SetObjectField(TEXT("Slot"), SerializeSlot(Node.Slot));

	if (Node.Properties.Num() > 0)
	{
		TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
		Node.Properties.ForEachSerialized([&Props](const FString& Key, const FString& Value)
		{
			if (Key == TEXT("ActiveWidgetIndex"))
			{
				return;
			}
			Props->SetStringField(Key, Value);
		});
		if (Props->Values.Num() > 0)
		{
			J->SetObjectField(TEXT("Properties"), Props);
		}
	}

	// TextStyle (v2.4)
	if (Node.TextStyle.FontSize > 0 || Node.TextStyle.SourceFontSize > 0 || Node.TextStyle.bHasLetterSpacing)
	{
		TSharedRef<FJsonObject> TS = MakeShared<FJsonObject>();
		if (Node.TextStyle.FontSize > 0)
			TS->SetNumberField(TEXT("FontSize"), Node.TextStyle.FontSize);
		if (Node.TextStyle.SourceFontSize > 0)
			TS->SetNumberField(TEXT("SourceFontSize"), Node.TextStyle.SourceFontSize);
		if (Node.TextStyle.bHasLetterSpacing)
			TS->SetNumberField(TEXT("LetterSpacing"), Node.TextStyle.LetterSpacing);
		J->SetObjectField(TEXT("TextStyle"), TS);
	}

	// Brush (v2.4)
	{
		const FUMGVisualBrushData& B = Node.Brush;
		TSharedRef<FJsonObject> BJ = MakeShared<FJsonObject>();
		bool bHasAny = false;
		if (B.OutlineCornerRadius >= 0)    { BJ->SetNumberField(TEXT("OutlineCornerRadius"), B.OutlineCornerRadius); bHasAny = true; }
		if (B.HasCornerRadii())            { BJ->SetStringField(TEXT("CornerRadii"), FString::Printf(TEXT("%.2f,%.2f,%.2f,%.2f"), B.CornerRadii.X, B.CornerRadii.Y, B.CornerRadii.Z, B.CornerRadii.W)); bHasAny = true; }
		if (B.OutlineThickness > 0)        { BJ->SetNumberField(TEXT("OutlineThickness"), B.OutlineThickness); bHasAny = true; }
		if (B.bHasDrawAs)                  { BJ->SetNumberField(TEXT("DrawAs"), (int32)B.DrawAs); bHasAny = true; }
		if (B.bHasBrushColor)              { BJ->SetStringField(TEXT("BrushColor"), B.BrushColor.ToString()); bHasAny = true; }
		if (B.bHasOutlineColor)            { BJ->SetStringField(TEXT("OutlineColor"), B.OutlineColor.ToString()); bHasAny = true; }
		if (!B.MaterialRef.IsEmpty())      { BJ->SetStringField(TEXT("MaterialRef"), B.MaterialRef); bHasAny = true; }
		if (!B.MaterialParams.IsEmpty())   { BJ->SetStringField(TEXT("MaterialParams"), B.MaterialParams); bHasAny = true; }
		if (!B.TexturePath.IsEmpty())      { BJ->SetStringField(TEXT("TexturePath"), B.TexturePath); bHasAny = true; }
		if (bHasAny)
			J->SetObjectField(TEXT("Brush"), BJ);
	}

	// Effect (v2.5)
	if (Node.Effect.HasAny())
	{
		const FUMGEffectData& E = Node.Effect;
		TSharedRef<FJsonObject> EJ = MakeShared<FJsonObject>();
		if (E.bHasBlurStrength)
			EJ->SetNumberField(TEXT("BlurStrength"), E.BlurStrength);
		if (E.bHasRetainRendering)
			EJ->SetBoolField(TEXT("RetainRendering"), E.bRetainRendering);
		if (!E.EffectMaterial.IsEmpty())
			EJ->SetStringField(TEXT("EffectMaterial"), E.EffectMaterial);
		if (!E.TextureParameter.IsEmpty())
			EJ->SetStringField(TEXT("TextureParameter"), E.TextureParameter);
		if (E.bHasEffectBlurUVStep)
		{
			EJ->SetStringField(
				TEXT("EffectBlurUVStep"),
				FString::Printf(TEXT("%.6f,%.6f"), E.EffectBlurUVStep.X, E.EffectBlurUVStep.Y));
		}
		if (E.bHasEffectColor)
			EJ->SetStringField(TEXT("EffectColor"), E.EffectColor.ToString());
		if (E.bHasEffectOffset)
		{
			EJ->SetStringField(
				TEXT("EffectOffset"),
				FString::Printf(TEXT("%.6f,%.6f"), E.EffectOffset.X, E.EffectOffset.Y));
		}
		if (E.bHasRenderingPhase)
		{
			EJ->SetNumberField(TEXT("Phase"), E.Phase);
			EJ->SetNumberField(TEXT("PhaseCount"), E.PhaseCount);
		}
		J->SetObjectField(TEXT("Effect"), EJ);
	}

	// WidgetState (v2.6)
	if (Node.WidgetState.HasAny())
	{
		const FUMGWidgetStateData& State = Node.WidgetState;
		TSharedRef<FJsonObject> StateObject = MakeShared<FJsonObject>();
		if (State.bHasActiveWidgetIndex)
		{
			StateObject->SetNumberField(TEXT("ActiveWidgetIndex"), State.ActiveWidgetIndex);
		}
		J->SetObjectField(TEXT("WidgetState"), StateObject);
	}

	if (Node.Children.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		Arr.Reserve(Node.Children.Num());
		for (const FUMGWidgetNode& Child : Node.Children)
			Arr.Add(MakeShared<FJsonValueObject>(SerializeWidgetNode(Child)));
		J->SetArrayField(TEXT("Children"), Arr);
	}

	return J;
}

FString FUMGJsonSerializer::Serialize(const FUMGWidgetData& Data)
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("WidgetName"), Data.WidgetName);
	Root->SetObjectField(TEXT("RootWidget"), SerializeWidgetNode(Data.RootWidget));

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Root, Writer);
	return Output;
}

