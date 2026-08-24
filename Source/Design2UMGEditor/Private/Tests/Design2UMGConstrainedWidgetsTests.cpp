// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Design2UMGConstrainedWidgets.h"

#include "Components/SizeBox.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesign2UMGConstrainedTextBlockDesiredSizeTest,
	"Design2UMG.WidgetTree.ConstrainedTextBlockDesiredSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesign2UMGConstrainedTextBlockDesiredSizeTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.WidgetStyle.WidthOverride = 160.0f;
	Node.WidgetStyle.HeightOverride = 58.0f;

	UDesign2UMGConstrainedTextBlock* Constrained = NewObject<UDesign2UMGConstrainedTextBlock>(GetTransientPackage());
	Constrained->SetText(FText::FromString(TEXT("A long enough value to expose natural size changes")));
	Constrained->ApplyDesign2UMGSizeConstraints(Node);
	const TSharedRef<SWidget> ConstrainedSlate = Constrained->TakeWidget();
	ConstrainedSlate->SlatePrepass();

	USizeBox* ReferenceSizeBox = NewObject<USizeBox>(GetTransientPackage());
	UTextBlock* ReferenceText = NewObject<UTextBlock>(ReferenceSizeBox);
	ReferenceText->SetText(FText::FromString(TEXT("A long enough value to expose natural size changes")));
	ReferenceSizeBox->AddChild(ReferenceText);
	ReferenceSizeBox->SetWidthOverride(160.0f);
	ReferenceSizeBox->SetHeightOverride(58.0f);
	const TSharedRef<SWidget> ReferenceSlate = ReferenceSizeBox->TakeWidget();
	ReferenceSlate->SlatePrepass();

	const FVector2D ConstrainedDesired = ConstrainedSlate->GetDesiredSize();
	const FVector2D ReferenceDesired = ReferenceSlate->GetDesiredSize();
	TestTrue(TEXT("Fused text widget reports the SizeBox width"), FMath::IsNearlyEqual(ConstrainedDesired.X, ReferenceDesired.X, 0.01f));
	TestTrue(TEXT("Fused text widget reports the SizeBox height"), FMath::IsNearlyEqual(ConstrainedDesired.Y, ReferenceDesired.Y, 0.01f));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
