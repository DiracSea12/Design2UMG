// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGPropertyApplierInternal.h"

#include "Components/ScaleBox.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SWidget.h"

#if WITH_AUTOMATION_WORKER

namespace
{
	bool TestScaleBoxClipping(
		FAutomationTestBase& Test,
		const TCHAR* CaseLabel,
		EWidgetClipping InitialClipping,
		const TOptional<FString>& ExplicitClipping,
		EWidgetClipping ExpectedClipping)
	{
		UScaleBox* ScaleBox = NewObject<UScaleBox>(GetTransientPackage());
		Test.TestNotNull(FString::Printf(TEXT("%s creates ScaleBox"), CaseLabel), ScaleBox);
		if (!ScaleBox)
		{
			return false;
		}

		ScaleBox->SetClipping(InitialClipping);
		const TSharedRef<SWidget> SlateWidget = ScaleBox->TakeWidget();

		FUMGWidgetNode Node;
		Node.WidgetStyle.Stretch = TEXT("UserSpecifiedWithClipping");
		Node.Render.Clipping = ExplicitClipping;
		UMGPropertyApplierInternal::ApplyWidgetProperties(ScaleBox, Node);

		const bool bUMGMatches = ScaleBox->GetClipping() == ExpectedClipping;
		const bool bSlateMatches = SlateWidget->GetClipping() == ExpectedClipping;
		Test.TestTrue(FString::Printf(TEXT("%s keeps expected UMG clipping"), CaseLabel), bUMGMatches);
		Test.TestTrue(FString::Printf(TEXT("%s updates materialized Slate clipping"), CaseLabel), bSlateMatches);
		return bUMGMatches && bSlateMatches;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScaleBoxUserSpecifiedWithClippingCompatibilityTest,
	"Design2UMG.PropertyApplier.ScaleBoxUserSpecifiedWithClippingCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FScaleBoxUserSpecifiedWithClippingCompatibilityTest::RunTest(const FString& Parameters)
{
#if UE_VERSION_OLDER_THAN(5, 1, 0)
	const EWidgetClipping NativeDefaultClipping = EWidgetClipping::ClipToBounds;
#else
	const EWidgetClipping NativeDefaultClipping = EWidgetClipping::Inherit;
#endif

	bool bAllMatched = true;
	bAllMatched &= TestScaleBoxClipping(
		*this,
		TEXT("Default clipping"),
		EWidgetClipping::Inherit,
		TOptional<FString>(),
		NativeDefaultClipping);
	bAllMatched &= TestScaleBoxClipping(
		*this,
		TEXT("Explicit inherit clipping"),
		EWidgetClipping::ClipToBoundsAlways,
		TOptional<FString>(FString(TEXT("Inherit"))),
		NativeDefaultClipping);
	bAllMatched &= TestScaleBoxClipping(
		*this,
		TEXT("Existing non-inherit clipping"),
		EWidgetClipping::ClipToBoundsAlways,
		TOptional<FString>(),
		EWidgetClipping::ClipToBoundsAlways);
	bAllMatched &= TestScaleBoxClipping(
		*this,
		TEXT("Explicit non-inherit clipping"),
		EWidgetClipping::Inherit,
		TOptional<FString>(FString(TEXT("ClipToBoundsWithoutIntersecting"))),
		EWidgetClipping::ClipToBoundsWithoutIntersecting);
	return bAllMatched;
}

#endif // WITH_AUTOMATION_WORKER
