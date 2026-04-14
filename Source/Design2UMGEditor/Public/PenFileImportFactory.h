// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "PenFileImportFactory.generated.h"

/**
 * Factory for importing `.pen` design files.
 * Supports drag-and-drop and context-menu import from the Content Browser.
 */
UCLASS()
class UPenFileImportFactory : public UFactory
{
	GENERATED_BODY()

public:
	UPenFileImportFactory();

	// UFactory
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent,
		FName InName, EObjectFlags Flags, const FString& Filename,
		const TCHAR* Parms, FFeedbackContext* Warn, bool& bOutOperationCanceled) override;

	virtual bool FactoryCanImport(const FString& Filename) override;
};

