// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGEditorImportServices.h"
#include "EditorSubsystem.h"
#include "Design2UMGEditorImportStateSubsystem.generated.h"

/**
 * Editor-subsystem owner of the Figma/Pencil import services. Replaces the former
 * FImportOrchestratorTransientState function-local singleton in the import orchestrator:
 * the instance is created by UEditorEngine once editor modules are loaded and destroyed
 * during engine shutdown (UEditorEngine::PreExit), which covers every consumer of the
 * old DLL-lifetime static.
 */
UCLASS()
class UDesign2UMGImportStateSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * Instance lookup for the import entry points. Returns null only once editor
	 * subsystems have been deinitialized during engine shutdown, after which no
	 * import command can run anymore.
	 */
	static UDesign2UMGImportStateSubsystem* Get();

	Design2UMGEditorImportServices::FFigmaImportService& GetFigmaImportService();
	Design2UMGEditorImportServices::FPencilImportService& GetPencilImportService();

	/** Cancels in-flight Figma requests and shuts down the Pencil connector. Idempotent. */
	void Shutdown();

protected:
	//~ Begin USubsystem Interface
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

private:
	Design2UMGEditorImportServices::FFigmaImportService FigmaService;
	Design2UMGEditorImportServices::FPencilImportService PencilService;
};
