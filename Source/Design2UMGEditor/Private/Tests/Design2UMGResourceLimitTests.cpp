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
	const uint8 PublicV4[] = { 8, 8, 8, 8 };
	TestTrue(TEXT("Public IPv4 socket destination accepted"), FImagePayloadIO::IsPublicNetworkAddress(PublicV4));
	const uint8 PrivateV4[][4] = {
		{ 0, 0, 0, 0 }, { 10, 1, 2, 3 }, { 100, 64, 0, 1 }, { 127, 0, 0, 1 },
		{ 169, 254, 169, 254 }, { 172, 16, 0, 1 }, { 192, 168, 1, 1 },
		{ 192, 0, 0, 1 }, { 192, 0, 2, 1 }, { 192, 88, 99, 1 }, { 198, 18, 0, 1 },
		{ 198, 51, 100, 1 }, { 203, 0, 113, 1 }, { 224, 0, 0, 1 }, { 255, 255, 255, 255 }
	};
	for (const auto& Address : PrivateV4)
	{
		TestFalse(TEXT("Non-public resolved IPv4 destination rejected"), FImagePayloadIO::IsPublicNetworkAddress(Address));
	}
	const uint8 PublicV6[16] = { 0x26, 0x06, 0x47, 0x00, 0x47, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0x11, 0x11 };
	TestTrue(TEXT("Public IPv6 socket destination accepted"), FImagePayloadIO::IsPublicNetworkAddress(PublicV6));
	const uint8 PrivateV6[][16] = {
		{ 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1 },
		{ 0xfc }, { 0xfe, 0x80 }, { 0xff }, { 0x20, 0x02 },
		{ 0x20, 0x01, 0, 0 }, { 0x20, 0x01, 0x0d, 0xb8 }, { 0x3f, 0xff },
		{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 127, 0, 0, 1 }
	};
	for (const auto& Address : PrivateV6)
	{
		TestFalse(TEXT("Non-public or transitional IPv6 destination rejected"), FImagePayloadIO::IsPublicNetworkAddress(Address));
	}
	const uint8 PublicBoundaries[][4] = { { 100, 63, 255, 255 }, { 100, 128, 0, 1 }, { 172, 15, 255, 255 }, { 172, 32, 0, 1 } };
	for (const auto& Address : PublicBoundaries)
	{
		TestTrue(TEXT("Public addresses adjacent to restricted blocks remain accepted"), FImagePayloadIO::IsPublicNetworkAddress(Address));
	}
	TestFalse(TEXT("Unknown socket address family is rejected"), FImagePayloadIO::IsPublicNetworkAddress({}));
	for (int32 LastOctet = 0; LastOctet <= 255; ++LastOctet)
	{
		const uint8 Address[] = { 192, 0, 0, static_cast<uint8>(LastOctet) };
		const bool bGlobalAnycast = LastOctet == 9 || LastOctet == 10;
		TestEqual(TEXT("192.0.0/24 allows only globally reachable anycast exceptions"),
			FImagePayloadIO::IsPublicNetworkAddress(Address), bGlobalAnycast);
		TestEqual(TEXT("Literal URL agrees with the actual connection address policy"),
			FImagePayloadIO::ValidateRemoteUrl(FString::Printf(TEXT("https://192.0.0.%d/image.png"), LastOctet), false, Error), bGlobalAnycast);
	}
	TestTrue(TEXT("Payload at byte limit is accepted"), FImagePayloadIO::ValidateCompressedByteCount(8, Limits, Error));
	TestFalse(TEXT("Payload above byte limit is rejected"), FImagePayloadIO::ValidateCompressedByteCount(9, Limits, Error));
	TestFalse(TEXT("Rejected payload reports an error"), Error.IsEmpty());
	TestTrue(TEXT("A later valid payload clears the old error"), FImagePayloadIO::ValidateCompressedByteCount(8, Limits, Error));
	TestTrue(TEXT("Successful validation leaves no stale error"), Error.IsEmpty());

	TestTrue(TEXT("Dimensions at side and pixel limits are accepted"), FImagePayloadIO::ValidateDimensions(4, 3, Limits, Error));
	TestFalse(TEXT("Width above side limit is rejected"), FImagePayloadIO::ValidateDimensions(5, 1, Limits, Error));
	TestFalse(TEXT("Pixel count above limit is rejected"), FImagePayloadIO::ValidateDimensions(4, 4, Limits, Error));
	FImageResourceLimits BgraLimits;
	BgraLimits.MaxDimension = TNumericLimits<int32>::Max();
	BgraLimits.MaxPixels = TNumericLimits<int64>::Max();
	const int32 HugeSide = BgraLimits.MaxDimension;
	TestTrue(TEXT("Dimension policy does not impose a BGRA allocation limit"), BgraLimits.AllowsDimensions(HugeSide, HugeSide));
	TestFalse(TEXT("BGRA decoding preserves its additional byte representation limit"),
		FImagePayloadIO::ValidateDimensions(HugeSide, HugeSide, BgraLimits, Error));
	TestEqual(TEXT("BGRA byte limit keeps the original pixel diagnostic"), Error,
		FString::Printf(TEXT("Image pixel count exceeds the limit (%dx%d, limit %lld)"),
			HugeSide, HugeSide, static_cast<long long>(BgraLimits.MaxPixels)));

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
	TestFalse(TEXT("Documentation IPv4 addresses are not public destinations"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://203.0.113.1/image.png"), false, Error));
	TestTrue(TEXT("Public IPv4 literal accepted without unsafe override"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://8.8.8.8/image.png"), false, Error));
	TestTrue(TEXT("Public IPv6 literal accepted without unsafe override"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://[2606:4700:4700::1111]:443/image.png"), false, Error));
	TestFalse(TEXT("IPv6 loopback literal rejected"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://[::1]/image.png"), false, Error));
	TestFalse(TEXT("IPv4-mapped loopback literal rejected"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://[::ffff:127.0.0.1]/image.png"), false, Error));
	TestFalse(TEXT("IPv6 unique-local literal rejected"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://[fd00::1]/image.png"), false, Error));
	TestFalse(TEXT("Integer loopback alias canonicalized and rejected"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://2130706433/image.png"), false, Error));
	TestFalse(TEXT("Hex loopback alias canonicalized and rejected"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://0x7f000001/image.png"), false, Error));
	TestFalse(TEXT("Invalid suffix after IPv6 literal rejected"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://[2606:4700:4700::1111]invalid/image.png"), false, Error));
	TestFalse(TEXT("Invalid port rejected by the transport URL parser"), FImagePayloadIO::ValidateRemoteUrl(TEXT("https://8.8.8.8:70000/image.png"), false, Error));
	TestTrue(TEXT("Explicit unsafe option allows private IPv6 HTTP"), FImagePayloadIO::ValidateRemoteUrl(TEXT("http://[::1]/image.png"), true, Error));
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
