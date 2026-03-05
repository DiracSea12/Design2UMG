// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "PenFileImportFactory.generated.h"

/**
 * .pen 鏂囦欢瀵煎叆宸ュ巶
 * 鏀寔 Content Browser 鎷栨嫿鍜屽彸閿鍏? */
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

