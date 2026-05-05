#pragma once

#include "CoreMinimal.h"

namespace Design2UMGAutomationRender
{
	bool IsAutomationRenderActive();
	FString GetAutomationFontImportPath();
	void HandleConsoleCommand(const TArray<FString>& Args);
}
