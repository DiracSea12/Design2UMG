// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignRemoteImageImporterInternal.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGRemoteImageResourceLimitsTest,
	"Design2UMG.Generation.RemoteImageResourceLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGRemoteImageResourceLimitsTest::RunTest(const FString& Parameters)
{
	using DesignRemoteImageImporterInternal::FImagePayloadIO;
	using DesignRemoteImageImporterInternal::FImageResourceLimits;

	FImageResourceLimits Limits;
	Limits.MaxCompressedBytes = 8;
	Limits.MaxDimension = 4;
	Limits.MaxPixels = 12;
	Limits.MaxUrlsPerOperation = 2;

	FString Error;
	TestTrue(TEXT("Payload at byte limit is accepted"), FImagePayloadIO::ValidateCompressedByteCount(8, Limits, Error));
	TestFalse(TEXT("Payload above byte limit is rejected"), FImagePayloadIO::ValidateCompressedByteCount(9, Limits, Error));
	TestFalse(TEXT("Rejected payload reports an error"), Error.IsEmpty());
	TestTrue(TEXT("A later valid payload clears the old error"), FImagePayloadIO::ValidateCompressedByteCount(8, Limits, Error));
	TestTrue(TEXT("Successful validation leaves no stale error"), Error.IsEmpty());

	TestTrue(TEXT("Dimensions at side and pixel limits are accepted"), FImagePayloadIO::ValidateDimensions(4, 3, Limits, Error));
	TestFalse(TEXT("Width above side limit is rejected"), FImagePayloadIO::ValidateDimensions(5, 1, Limits, Error));
	TestFalse(TEXT("Pixel count above limit is rejected"), FImagePayloadIO::ValidateDimensions(4, 4, Limits, Error));

	TestTrue(TEXT("URL count at limit is accepted"), FImagePayloadIO::ValidateUrlCount(2, Limits, Error));
	TestFalse(TEXT("URL count above limit is rejected"), FImagePayloadIO::ValidateUrlCount(3, Limits, Error));
	return true;
}

#endif
