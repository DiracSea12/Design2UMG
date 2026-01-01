// Copyright Epic Games, Inc. All Rights Reserved.

#include "UMGSampleExtractor.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"

FBounds FUMGSampleExtractor::SlotToBounds(const FUMGSlotData& Slot, const FIntPoint& DesignSize)
{
	FBounds Bounds;
	const float W = DesignSize.X;
	const float H = DesignSize.Y;

	// 点锚点模式（Min == Max）
	bool bPointAnchorX = FMath::IsNearlyEqual(Slot.Anchors.Min.X, Slot.Anchors.Max.X, 0.001f);
	bool bPointAnchorY = FMath::IsNearlyEqual(Slot.Anchors.Min.Y, Slot.Anchors.Max.Y, 0.001f);

	if (bPointAnchorX)
	{
		// X: 点锚点，Offsets.Left是位置偏移，Offsets.Right是宽度
		float AnchorX = Slot.Anchors.Min.X;
		float AlignX = Slot.Alignment.X;
		float WidgetW = Slot.Offsets.Right;
		float AnchorPixelX = AnchorX * W;
		float AlignPointPixelX = AnchorPixelX + Slot.Offsets.Left;
		float LeftPixelX = AlignPointPixelX - AlignX * WidgetW;
		Bounds.X = LeftPixelX / W;
		Bounds.W = WidgetW / W;
	}
	else
	{
		// X: 拉伸锚点，Offsets.Left是左边距，Offsets.Right是右边距
		Bounds.X = (Slot.Anchors.Min.X * W + Slot.Offsets.Left) / W;
		float RightX = Slot.Anchors.Max.X * W - Slot.Offsets.Right;
		Bounds.W = (RightX - Bounds.X * W) / W;
	}

	if (bPointAnchorY)
	{
		float AnchorY = Slot.Anchors.Min.Y;
		float AlignY = Slot.Alignment.Y;
		float WidgetH = Slot.Offsets.Bottom;
		float AnchorPixelY = AnchorY * H;
		float AlignPointPixelY = AnchorPixelY + Slot.Offsets.Top;
		float TopPixelY = AlignPointPixelY - AlignY * WidgetH;
		Bounds.Y = TopPixelY / H;
		Bounds.H = WidgetH / H;
	}
	else
	{
		Bounds.Y = (Slot.Anchors.Min.Y * H + Slot.Offsets.Top) / H;
		float BottomY = Slot.Anchors.Max.Y * H - Slot.Offsets.Bottom;
		Bounds.H = (BottomY - Bounds.Y * H) / H;
	}

	return Bounds;
}

int32 FUMGSampleExtractor::ExtractFromWidgetBlueprint(
	const FString& WidgetBlueprintPath,
	TArray<FUMGExtractedSample>& OutSamples)
{
	UWidgetBlueprint* WidgetBP = LoadObject<UWidgetBlueprint>(nullptr, *WidgetBlueprintPath);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		UE_LOG(LogTemp, Error, TEXT("无法加载Widget Blueprint: %s"), *WidgetBlueprintPath);
		return 0;
	}

	FIntPoint DesignSize(1920, 1080); // 默认设计尺寸
	int32 Count = 0;

	WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (!Widget) return;

		UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot);
		if (!CanvasSlot) return;

		FUMGExtractedSample Sample;
		Sample.WidgetName = Widget->GetName();
		Sample.WidgetType = Widget->GetClass()->GetName();

		// 提取原始Slot数据
		FAnchors Anchors = CanvasSlot->GetAnchors();
		Sample.OriginalSlot.Anchors.Min = {static_cast<float>(Anchors.Minimum.X), static_cast<float>(Anchors.Minimum.Y)};
		Sample.OriginalSlot.Anchors.Max = {static_cast<float>(Anchors.Maximum.X), static_cast<float>(Anchors.Maximum.Y)};
		Sample.OriginalSlot.Alignment = {static_cast<float>(CanvasSlot->GetAlignment().X), static_cast<float>(CanvasSlot->GetAlignment().Y)};
		FMargin Offsets = CanvasSlot->GetOffsets();
		Sample.OriginalSlot.Offsets = {Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom};

		// 反推Bounds
		Sample.Bounds = SlotToBounds(Sample.OriginalSlot, DesignSize);

		OutSamples.Add(Sample);
		Count++;
	});

	UE_LOG(LogTemp, Log, TEXT("从 %s 提取了 %d 个样例"), *WidgetBlueprintPath, Count);
	return Count;
}

bool FUMGSampleExtractor::ExportToJson(
	const TArray<FUMGExtractedSample>& Samples,
	const FString& OutputPath)
{
	TSharedRef<FJsonObject> RootObj = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SamplesArray;

	for (const FUMGExtractedSample& Sample : Samples)
	{
		TSharedRef<FJsonObject> SampleObj = MakeShared<FJsonObject>();
		SampleObj->SetStringField(TEXT("Name"), Sample.WidgetName);
		SampleObj->SetStringField(TEXT("Type"), Sample.WidgetType);

		// Bounds
		TSharedRef<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetNumberField(TEXT("X"), Sample.Bounds.X);
		BoundsObj->SetNumberField(TEXT("Y"), Sample.Bounds.Y);
		BoundsObj->SetNumberField(TEXT("W"), Sample.Bounds.W);
		BoundsObj->SetNumberField(TEXT("H"), Sample.Bounds.H);
		SampleObj->SetObjectField(TEXT("Bounds"), BoundsObj);

		// Slot
		TSharedRef<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetNumberField(TEXT("AnchorMinX"), Sample.OriginalSlot.Anchors.Min.X);
		SlotObj->SetNumberField(TEXT("AnchorMinY"), Sample.OriginalSlot.Anchors.Min.Y);
		SlotObj->SetNumberField(TEXT("AnchorMaxX"), Sample.OriginalSlot.Anchors.Max.X);
		SlotObj->SetNumberField(TEXT("AnchorMaxY"), Sample.OriginalSlot.Anchors.Max.Y);
		SlotObj->SetNumberField(TEXT("AlignX"), Sample.OriginalSlot.Alignment.X);
		SlotObj->SetNumberField(TEXT("AlignY"), Sample.OriginalSlot.Alignment.Y);
		SlotObj->SetNumberField(TEXT("Left"), Sample.OriginalSlot.Offsets.Left);
		SlotObj->SetNumberField(TEXT("Top"), Sample.OriginalSlot.Offsets.Top);
		SlotObj->SetNumberField(TEXT("Right"), Sample.OriginalSlot.Offsets.Right);
		SlotObj->SetNumberField(TEXT("Bottom"), Sample.OriginalSlot.Offsets.Bottom);
		SampleObj->SetObjectField(TEXT("Slot"), SlotObj);

		SamplesArray.Add(MakeShared<FJsonValueObject>(SampleObj));
	}

	RootObj->SetArrayField(TEXT("Samples"), SamplesArray);

	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(RootObj, Writer);

	return FFileHelper::SaveStringToFile(JsonString, *OutputPath);
}

bool FUMGSampleExtractor::LoadFromJson(
	const FString& JsonPath,
	TArray<FUMGExtractedSample>& OutSamples)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *JsonPath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* SamplesArray;
	if (!RootObj->TryGetArrayField(TEXT("Samples"), SamplesArray))
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *SamplesArray)
	{
		const TSharedPtr<FJsonObject>* SampleObj;
		if (!Value->TryGetObject(SampleObj)) continue;

		FUMGExtractedSample Sample;
		Sample.WidgetName = (*SampleObj)->GetStringField(TEXT("Name"));
		Sample.WidgetType = (*SampleObj)->GetStringField(TEXT("Type"));

		// Bounds
		const TSharedPtr<FJsonObject>* BoundsObj;
		if ((*SampleObj)->TryGetObjectField(TEXT("Bounds"), BoundsObj))
		{
			Sample.Bounds.X = (*BoundsObj)->GetNumberField(TEXT("X"));
			Sample.Bounds.Y = (*BoundsObj)->GetNumberField(TEXT("Y"));
			Sample.Bounds.W = (*BoundsObj)->GetNumberField(TEXT("W"));
			Sample.Bounds.H = (*BoundsObj)->GetNumberField(TEXT("H"));
		}

		// Slot
		const TSharedPtr<FJsonObject>* SlotObj;
		if ((*SampleObj)->TryGetObjectField(TEXT("Slot"), SlotObj))
		{
			Sample.OriginalSlot.Anchors.Min.X = (*SlotObj)->GetNumberField(TEXT("AnchorMinX"));
			Sample.OriginalSlot.Anchors.Min.Y = (*SlotObj)->GetNumberField(TEXT("AnchorMinY"));
			Sample.OriginalSlot.Anchors.Max.X = (*SlotObj)->GetNumberField(TEXT("AnchorMaxX"));
			Sample.OriginalSlot.Anchors.Max.Y = (*SlotObj)->GetNumberField(TEXT("AnchorMaxY"));
			Sample.OriginalSlot.Alignment.X = (*SlotObj)->GetNumberField(TEXT("AlignX"));
			Sample.OriginalSlot.Alignment.Y = (*SlotObj)->GetNumberField(TEXT("AlignY"));
			Sample.OriginalSlot.Offsets.Left = (*SlotObj)->GetNumberField(TEXT("Left"));
			Sample.OriginalSlot.Offsets.Top = (*SlotObj)->GetNumberField(TEXT("Top"));
			Sample.OriginalSlot.Offsets.Right = (*SlotObj)->GetNumberField(TEXT("Right"));
			Sample.OriginalSlot.Offsets.Bottom = (*SlotObj)->GetNumberField(TEXT("Bottom"));
		}

		OutSamples.Add(Sample);
	}

	return OutSamples.Num() > 0;
}
