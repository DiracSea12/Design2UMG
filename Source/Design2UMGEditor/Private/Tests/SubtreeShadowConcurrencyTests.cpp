// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved.
// SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "Design2UMGEffectMaterialBuilder.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
// Run in two real-project editors with the same fresh -D2UShadowCreationNonce.
// Opt-in discovery avoids presenting an unexecuted cross-process test as a pass.
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FSubtreeShadowConcurrentCreationTest,
	"Design2UMG.Converter.SubtreeShadowConcurrentCreation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

void FSubtreeShadowConcurrentCreationTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	FString Nonce;
	if (FParse::Value(FCommandLine::Get(), TEXT("D2UShadowCreationNonce="), Nonce))
	{
		Names.Add(TEXT("TwoEditorsCreateFreshMaterials"));
		Commands.Add(Nonce);
	}
}

bool FSubtreeShadowConcurrentCreationTest::RunTest(const FString& Nonce)
{
	const uint32 Seed = FCrc::StrCrc32(*Nonce);
	const FString Directory = FPaths::ProjectSavedDir() / TEXT("Design2UMG/ShadowConcurrency")
		/ FString::Printf(TEXT("%08x"), Seed);
	IFileManager::Get().MakeDirectory(*Directory, true);
	for (int32 Round = 0; Round < 3; ++Round)
	{
		const FString Marker = Directory / FString::Printf(TEXT("%d_%u.ready"), Round, FPlatformProcess::GetCurrentProcessId());
		if (!TestTrue(TEXT("Publish participant"), FFileHelper::SaveStringToFile(TEXT("ready"), *Marker))) return false;
		const double Deadline = FPlatformTime::Seconds() + 60.0;
		TArray<FString> Participants;
		do
		{
			Participants.Reset();
			IFileManager::Get().FindFiles(Participants, *(Directory / FString::Printf(TEXT("%d_*.ready"), Round)), true, false);
			if (Participants.Num() == 2) break;
			FPlatformProcess::Sleep(0.01f);
		} while (FPlatformTime::Seconds() < Deadline);
		if (!TestEqual(TEXT("Exactly two editors reached the creation barrier"), Participants.Num(), 2)) return false;

		FUMGWidgetNode Root;
		Root.Type = TEXT("RetainerBox");
		Root.Name = TEXT("ConcurrentShadow");
		Root.Bounds.W = 128.0f + static_cast<float>(Seed & 0xffff) / 1024.0f;
		Root.Bounds.H = 128.0f + static_cast<float>(Seed >> 16) / 1024.0f;
		Root.Effect.EffectMaterial = UMGPropertyKeys::RetainerSubtreeShadowEffectRef;
		FUMGBoxShadowLayerData Layer;
		Layer.Color = FLinearColor(0.1f, 0.2f, 0.3f, 0.5f);
		Layer.Offset = FVector2D(1 + Round, 2);
		Root.Shadow.BoxShadows.Add(Layer);
		FString Error;
		const bool bPrepared = Design2UMGEffectMaterialBuilder::PrepareSubtreeShadowMaterials(Root, Error);
		TestTrue(FString::Printf(TEXT("Both editors prepare round %d: %s"), Round, *Error), bPrepared);
		if (bPrepared)
		{
			const FString ObjectPath = Root.Effect.EffectMaterial.Get(FString());
			TestTrue(TEXT("Shared material is persisted"), FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath)));
			AddInfo(FString::Printf(TEXT("Round %d material=%s"), Round, *ObjectPath));
		}
	}
	return true;
}
#endif
