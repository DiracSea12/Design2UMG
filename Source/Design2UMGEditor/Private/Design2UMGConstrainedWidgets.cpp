// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConstrainedWidgets.h"

#include "Widgets/Layout/SBox.h"

namespace
{
	FOptionalSize ToOptionalSize(const bool bSet, const float Value)
	{
		return bSet ? FOptionalSize(Value) : FOptionalSize();
	}

	FDesign2UMGSizeConstraintProperties MakeProperties(const FUMGWidgetNode& Node)
	{
		FDesign2UMGSizeConstraintProperties Properties;
		Properties.bWidthOverride = Node.WidgetStyle.WidthOverride.IsSet();
		Properties.WidthOverride = Node.WidgetStyle.WidthOverride.Get(0.0f);
		Properties.bHeightOverride = Node.WidgetStyle.HeightOverride.IsSet();
		Properties.HeightOverride = Node.WidgetStyle.HeightOverride.Get(0.0f);
		Properties.bMinDesiredWidth = Node.WidgetStyle.MinDesiredWidth.IsSet();
		Properties.MinDesiredWidth = Node.WidgetStyle.MinDesiredWidth.Get(0.0f);
		Properties.bMinDesiredHeight = Node.WidgetStyle.MinDesiredHeight.IsSet();
		Properties.MinDesiredHeight = Node.WidgetStyle.MinDesiredHeight.Get(0.0f);
		return Properties;
	}
}

void ApplyDesign2UMGSizeConstraints(UWidget* Widget, const FUMGWidgetNode& Node)
{
	if (UDesign2UMGConstrainedBorder* Constrained = Cast<UDesign2UMGConstrainedBorder>(Widget))
	{
		Constrained->ApplyDesign2UMGSizeConstraints(Node);
	}
	else if (UDesign2UMGConstrainedCanvasPanel* ConstrainedCanvas = Cast<UDesign2UMGConstrainedCanvasPanel>(Widget))
	{
		ConstrainedCanvas->ApplyDesign2UMGSizeConstraints(Node);
	}
	else if (UDesign2UMGConstrainedHorizontalBox* ConstrainedHorizontal = Cast<UDesign2UMGConstrainedHorizontalBox>(Widget))
	{
		ConstrainedHorizontal->ApplyDesign2UMGSizeConstraints(Node);
	}
	else if (UDesign2UMGConstrainedImage* ConstrainedImage = Cast<UDesign2UMGConstrainedImage>(Widget))
	{
		ConstrainedImage->ApplyDesign2UMGSizeConstraints(Node);
	}
	else if (UDesign2UMGConstrainedOverlay* ConstrainedOverlay = Cast<UDesign2UMGConstrainedOverlay>(Widget))
	{
		ConstrainedOverlay->ApplyDesign2UMGSizeConstraints(Node);
	}
	else if (UDesign2UMGConstrainedRetainerBox* ConstrainedRetainer = Cast<UDesign2UMGConstrainedRetainerBox>(Widget))
	{
		ConstrainedRetainer->ApplyDesign2UMGSizeConstraints(Node);
	}
	else if (UDesign2UMGConstrainedTextBlock* ConstrainedText = Cast<UDesign2UMGConstrainedTextBlock>(Widget))
	{
		ConstrainedText->ApplyDesign2UMGSizeConstraints(Node);
	}
	else if (UDesign2UMGConstrainedVerticalBox* ConstrainedVertical = Cast<UDesign2UMGConstrainedVerticalBox>(Widget))
	{
		ConstrainedVertical->ApplyDesign2UMGSizeConstraints(Node);
	}
	else if (UDesign2UMGConstrainedWrapBox* ConstrainedWrap = Cast<UDesign2UMGConstrainedWrapBox>(Widget))
	{
		ConstrainedWrap->ApplyDesign2UMGSizeConstraints(Node);
	}
}

TSharedRef<SWidget> FDesign2UMGSizeConstraintSlateBridge::Wrap(
	const TSharedRef<SWidget>& Content,
	const FDesign2UMGSizeConstraintProperties& Properties)
{
	MySizeBox = SNew(SBox)
		.WidthOverride(ToOptionalSize(Properties.bWidthOverride, Properties.WidthOverride))
		.HeightOverride(ToOptionalSize(Properties.bHeightOverride, Properties.HeightOverride))
		.MinDesiredWidth(ToOptionalSize(Properties.bMinDesiredWidth, Properties.MinDesiredWidth))
		.MinDesiredHeight(ToOptionalSize(Properties.bMinDesiredHeight, Properties.MinDesiredHeight))
		[
			Content
		];
	return MySizeBox.ToSharedRef();
}

void FDesign2UMGSizeConstraintSlateBridge::Apply(const FDesign2UMGSizeConstraintProperties& Properties)
{
	if (!MySizeBox.IsValid())
	{
		return;
	}

	MySizeBox->SetWidthOverride(ToOptionalSize(Properties.bWidthOverride, Properties.WidthOverride));
	MySizeBox->SetHeightOverride(ToOptionalSize(Properties.bHeightOverride, Properties.HeightOverride));
	MySizeBox->SetMinDesiredWidth(ToOptionalSize(Properties.bMinDesiredWidth, Properties.MinDesiredWidth));
	MySizeBox->SetMinDesiredHeight(ToOptionalSize(Properties.bMinDesiredHeight, Properties.MinDesiredHeight));
}

void FDesign2UMGSizeConstraintSlateBridge::Release()
{
	MySizeBox.Reset();
}

#define DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(ClassName, BaseClass) \
ClassName::ClassName(const FObjectInitializer& ObjectInitializer) \
	: Super(ObjectInitializer) \
{ \
} \
\
void ClassName::ApplyDesign2UMGSizeConstraints(const FUMGWidgetNode& Node) \
{ \
	SizeConstraints = MakeProperties(Node); \
	SizeConstraintBridge.Apply(SizeConstraints); \
} \
\
void ClassName::SynchronizeProperties() \
{ \
	Super::SynchronizeProperties(); \
	SizeConstraintBridge.Apply(SizeConstraints); \
} \
\
void ClassName::ReleaseSlateResources(const bool bReleaseChildren) \
{ \
	Super::ReleaseSlateResources(bReleaseChildren); \
	SizeConstraintBridge.Release(); \
} \
\
TSharedRef<SWidget> ClassName::RebuildWidget() \
{ \
	return SizeConstraintBridge.Wrap(Super::RebuildWidget(), SizeConstraints); \
}

DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedBorder, UBorder)
DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedCanvasPanel, UCanvasPanel)
DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedHorizontalBox, UHorizontalBox)
DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedImage, UImage)
DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedOverlay, UOverlay)
DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedRetainerBox, URetainerBox)
DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedTextBlock, UTextBlock)
DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedVerticalBox, UVerticalBox)
DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION(UDesign2UMGConstrainedWrapBox, UWrapBox)

#undef DESIGN2UMG_CONSTRAINED_WIDGET_IMPLEMENTATION
