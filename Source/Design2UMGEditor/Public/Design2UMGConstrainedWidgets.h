// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"
#include "Blueprint/UserWidget.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/RetainerBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "Design2UMGConstrainedWidgets.generated.h"

class SBox;
class SWidget;
class UWidget;

USTRUCT(BlueprintType)
struct DESIGN2UMGEDITOR_API FDesign2UMGSizeConstraintProperties
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	bool bWidthOverride = false;

	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	float WidthOverride = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	bool bHeightOverride = false;

	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	float HeightOverride = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	bool bMinDesiredWidth = false;

	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	float MinDesiredWidth = 0.0f;

	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	bool bMinDesiredHeight = false;

	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	float MinDesiredHeight = 0.0f;
};

DESIGN2UMGEDITOR_API void ApplyDesign2UMGSizeConstraints(UWidget* Widget, const FUMGWidgetNode& Node);

class DESIGN2UMGEDITOR_API FDesign2UMGSizeConstraintSlateBridge
{
public:
	TSharedRef<SWidget> Wrap(const TSharedRef<SWidget>& Content, const FDesign2UMGSizeConstraintProperties& Properties);
	void Apply(const FDesign2UMGSizeConstraintProperties& Properties);
	void Release();

private:
	TSharedPtr<SBox> MySizeBox;
};

#define DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS() \
public: \
	void ApplyDesign2UMGSizeConstraints(const FUMGWidgetNode& Node); \
	virtual void SynchronizeProperties() override; \
protected: \
	virtual void ReleaseSlateResources(bool bReleaseChildren) override; \
	virtual TSharedRef<SWidget> RebuildWidget() override; \
private: \
	FDesign2UMGSizeConstraintSlateBridge SizeConstraintBridge;

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedBorder : public UBorder
{
	GENERATED_BODY()
	UDesign2UMGConstrainedBorder(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedCanvasPanel : public UCanvasPanel
{
	GENERATED_BODY()
	UDesign2UMGConstrainedCanvasPanel(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedHorizontalBox : public UHorizontalBox
{
	GENERATED_BODY()
	UDesign2UMGConstrainedHorizontalBox(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedImage : public UImage
{
	GENERATED_BODY()
	UDesign2UMGConstrainedImage(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedOverlay : public UOverlay
{
	GENERATED_BODY()
	UDesign2UMGConstrainedOverlay(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedRetainerBox : public URetainerBox
{
	GENERATED_BODY()
	UDesign2UMGConstrainedRetainerBox(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedTextBlock : public UTextBlock
{
	GENERATED_BODY()
	UDesign2UMGConstrainedTextBlock(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedVerticalBox : public UVerticalBox
{
	GENERATED_BODY()
	UDesign2UMGConstrainedVerticalBox(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGConstrainedWrapBox : public UWrapBox
{
	GENERATED_BODY()
	UDesign2UMGConstrainedWrapBox(const FObjectInitializer& ObjectInitializer);
	DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS()
	UPROPERTY(EditAnywhere, Category = "Design2UMG|Size Constraint")
	FDesign2UMGSizeConstraintProperties SizeConstraints;
};

#undef DESIGN2UMG_CONSTRAINED_WIDGET_MEMBERS
