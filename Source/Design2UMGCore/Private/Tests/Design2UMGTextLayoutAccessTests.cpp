// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGTextLayoutAccess.h"

#include "Components/TextBlock.h"
#include "Fonts/FontCache.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGTextLayoutAccessSynchronizationTest,
	"Design2UMG.TextLayoutAccess.PreAndPostSlateSynchronization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGTextLayoutAccessSynchronizationTest::RunTest(const FString&)
{
	UTextBlock* TextBlock = NewObject<UTextBlock>(GetTransientPackage());
	if (!TestNotNull(TEXT("Text block"), TextBlock))
	{
		return false;
	}

	TextBlock->SetText(FText::FromString(TEXT("Alpha Beta Gamma Delta Epsilon Zeta")));

	Design2UMGTextLayoutAccess::FUpdate Initial;
	Initial.LineHeightPercentage = 1.0f;
	Initial.ApplyLineHeightToBottomLine = false;
	Initial.AutoWrapText = false;
	Initial.WrapTextAt = 64.0f;
	Initial.Margin = FMargin(4.0f, 5.0f, 6.0f, 7.0f);
	FShapedTextOptions ShapedTextOptions;
	ShapedTextOptions.bOverride_TextShapingMethod = true;
	ShapedTextOptions.TextShapingMethod = ETextShapingMethod::KerningOnly;
	ShapedTextOptions.bOverride_TextFlowDirection = true;
	ShapedTextOptions.TextFlowDirection = ETextFlowDirection::RightToLeft;
	Initial.ShapedTextOptions = ShapedTextOptions;
	Design2UMGTextLayoutAccess::Apply(*TextBlock, Initial);

	FStructProperty* MarginProperty = FindFProperty<FStructProperty>(
		UTextLayoutWidget::StaticClass(), TEXT("Margin"));
	if (!TestNotNull(TEXT("Margin property"), MarginProperty))
	{
		return false;
	}
	const FMargin* AppliedMargin = MarginProperty->ContainerPtrToValuePtr<FMargin>(TextBlock);
	TestEqual(TEXT("Margin left copied"), AppliedMargin->Left, 4.0f);
	TestEqual(TEXT("Margin top copied"), AppliedMargin->Top, 5.0f);
	TestEqual(TEXT("Margin right copied"), AppliedMargin->Right, 6.0f);
	TestEqual(TEXT("Margin bottom copied"), AppliedMargin->Bottom, 7.0f);

	FStructProperty* ShapedTextOptionsProperty = FindFProperty<FStructProperty>(
		UTextLayoutWidget::StaticClass(), TEXT("ShapedTextOptions"));
	if (!TestNotNull(TEXT("Shaped text options property"), ShapedTextOptionsProperty))
	{
		return false;
	}
	const FShapedTextOptions* AppliedShapedTextOptions =
		ShapedTextOptionsProperty->ContainerPtrToValuePtr<FShapedTextOptions>(TextBlock);
	TestTrue(TEXT("Text shaping override copied"), AppliedShapedTextOptions->bOverride_TextShapingMethod != 0);
	TestTrue(TEXT("Text shaping method copied"),
		AppliedShapedTextOptions->TextShapingMethod == ETextShapingMethod::KerningOnly);
	TestTrue(TEXT("Text flow override copied"), AppliedShapedTextOptions->bOverride_TextFlowDirection != 0);
	TestTrue(TEXT("Text flow direction copied"),
		AppliedShapedTextOptions->TextFlowDirection == ETextFlowDirection::RightToLeft);

	const TSharedRef<SWidget> SlateWidget = TextBlock->TakeWidget();
	SlateWidget->SlatePrepass(1.0f);
	const FVector2D InitialSize = SlateWidget->GetDesiredSize();
	TestTrue(TEXT("Pre-TakeWidget update reaches Slate"), InitialSize.X > 0.0f && InitialSize.Y > 0.0f);

	Design2UMGTextLayoutAccess::FUpdate Taller;
	Taller.LineHeightPercentage = 1.75f;
	Taller.ApplyLineHeightToBottomLine = true;
	Design2UMGTextLayoutAccess::Apply(*TextBlock, Taller);
	SlateWidget->SlatePrepass(1.0f);
	const FVector2D TallerSize = SlateWidget->GetDesiredSize();
	TestTrue(TEXT("Post-TakeWidget line height resynchronizes Slate"), TallerSize.Y > InitialSize.Y);

	Design2UMGTextLayoutAccess::FUpdate Wider;
	Wider.LineHeightPercentage = 1.0f;
	Wider.ApplyLineHeightToBottomLine = false;
	Wider.WrapTextAt = 512.0f;
	Design2UMGTextLayoutAccess::Apply(*TextBlock, Wider);
	SlateWidget->SlatePrepass(1.0f);
	const FVector2D WiderSize = SlateWidget->GetDesiredSize();
	TestTrue(TEXT("Post-TakeWidget wrap width resynchronizes Slate"), WiderSize.X > InitialSize.X);
	TestTrue(TEXT("Wider wrapping uses fewer lines"), WiderSize.Y < TallerSize.Y);

	return true;
}

#endif
