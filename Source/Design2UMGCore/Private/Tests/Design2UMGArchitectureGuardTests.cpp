// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"

#if WITH_AUTOMATION_WORKER
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextMeasurementPrivateBoundaryTest,
	"Design2UMG.Architecture.TextMeasurementPrivateBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextMeasurementPrivateBoundaryTest::RunTest(const FString&)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Design2UMG"));
	if (!TestTrue(TEXT("Plugin source location is available"), Plugin.IsValid())) return false;
	const FString Core = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/Design2UMGCore"));
	TestFalse(TEXT("TextMeasurement is not exposed as a public header"),
		IFileManager::Get().FileExists(*FPaths::Combine(Core, TEXT("Public/TextMeasurement.h"))));
	TestTrue(TEXT("Private TextMeasurement header exists"),
		IFileManager::Get().FileExists(*FPaths::Combine(Core, TEXT("Private/TextMeasurement.h"))));
	return true;
}
#endif
