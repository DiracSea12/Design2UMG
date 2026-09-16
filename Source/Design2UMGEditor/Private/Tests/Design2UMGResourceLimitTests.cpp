// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignRemoteImageImporterInternal.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDesign2UMGRemoteImageResourceLimitsTest,
	"Design2UMG.Generation.RemoteImageResourceLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDesign2UMGRemoteImageResourceLimitsTest::RunTest(const FString& Parameters)
{
	using DesignRemoteImageImporterInternal::FImagePayloadIO;
	using DesignImageResources::FImageOperationBudget;
	using DesignImageResources::FImageResourceLimits;

	FImageResourceLimits Limits;
	Limits.MaxCompressedBytes = 8;
	Limits.MaxDimension = 4;
	Limits.MaxPixels = 12;
	Limits.MaxUrlsPerOperation = 2;
	Limits.MaxTotalCompressedBytes = 16;
	Limits.MaxTotalDecodedBytes = 48;

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

	FImageOperationBudget Budget(Limits);
	TestTrue(TEXT("First compressed payload fits the operation budget"), Budget.ConsumeCompressedBytes(8, Error));
	TestTrue(TEXT("Second compressed payload reaches the operation budget"), Budget.ConsumeCompressedBytes(8, Error));
	TestFalse(TEXT("Compressed operation budget rejects additional data"), Budget.ConsumeCompressedBytes(1, Error));
	TestTrue(TEXT("Decoded pixels at the operation limit are accepted"), Budget.ConsumeDecodedPixels(12, Error));
	TestFalse(TEXT("Decoded operation budget rejects additional pixels"), Budget.ConsumeDecodedPixels(1, Error));

	TestTrue(TEXT("Public HTTPS image URL is accepted"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://cdn.example.com/image.png"), false, Error));
	TestFalse(TEXT("Plain HTTP image URL is rejected by default"), FImagePayloadIO::ValidateRemoteUrl(TEXT("http://cdn.example.com/image.png"), false, Error));
	TestFalse(TEXT("Loopback image URL is rejected by default"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://127.0.0.1/image.png"), false, Error));
	TestFalse(TEXT("Short numeric loopback URL is rejected by default"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://127.1/image.png"), false, Error));
	TestFalse(TEXT("Trailing-dot localhost URL is rejected by default"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://localhost./image.png"), false, Error));
	TestFalse(TEXT("Private IPv4 image URL is rejected by default"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://192.168.1.2/image.png"), false, Error));
	TestFalse(TEXT("Public IPv4 image URL is rejected by default"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://203.0.113.1/image.png"), false, Error));
	TestTrue(TEXT("Explicit unsafe URL setting permits HTTP fixtures"), FImagePayloadIO::ValidateRemoteUrl(TEXT("http://127.0.0.1/image.png"), true, Error));
	const FString OversizedFile = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("D2UImageLimit"), TEXT(".bin"));
	TArray<uint8> OversizedBytes;
	OversizedBytes.Init(0x5a, 9);
	if (TestTrue(TEXT("Oversized local fixture is written"), FFileHelper::SaveArrayToFile(OversizedBytes, *OversizedFile)))
	{
		TArray<uint8> LoadedBytes;
		TestFalse(TEXT("Oversized local file is rejected before loading"), FImagePayloadIO::LoadLocalFile(OversizedFile, Limits, LoadedBytes, Error));
		TestEqual(TEXT("Rejected local file does not allocate payload bytes"), LoadedBytes.Num(), 0);
	}
	IFileManager::Get().Delete(*OversizedFile);
	return true;
}

#endif
