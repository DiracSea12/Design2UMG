// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportStateSubsystem.h"

#include "Editor.h"

UDesign2UMGImportStateSubsystem* UDesign2UMGImportStateSubsystem::Get()
{
	return GEditor ? GEditor->GetEditorSubsystem<UDesign2UMGImportStateSubsystem>() : nullptr;
}

Design2UMGEditorImportServices::FFigmaImportService& UDesign2UMGImportStateSubsystem::GetFigmaImportService()
{
	return FigmaService;
}

Design2UMGEditorImportServices::FPencilImportService& UDesign2UMGImportStateSubsystem::GetPencilImportService()
{
	return PencilService;
}

void UDesign2UMGImportStateSubsystem::Shutdown()
{
	FigmaService.Invalidate();
	PencilService.Shutdown();
}

void UDesign2UMGImportStateSubsystem::Deinitialize()
{
	// Engine exit deinitializes editor subsystems (UEditorEngine::PreExit) before plugin
	// modules' ShutdownModule runs, while hot reload shuts the module down while this
	// subsystem is still alive. Both paths call the same idempotent Shutdown() so the
	// services are torn down no matter which one fires (running it twice is a harmless
	// generation bump plus null-guarded resets).
	Shutdown();
	Super::Deinitialize();
}
