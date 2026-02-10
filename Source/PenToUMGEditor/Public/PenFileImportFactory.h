// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "PenFileImportFactory.generated.h"

/**
 * .pen 文件导入工厂
 * 支持 Content Browser 拖拽和右键导入
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
