#pragma once

#include "CoreMinimal.h"

namespace Design2UMGEditorMenuOwner
{
	FDelegateHandle RegisterMenus();
	void UnregisterMenus(FDelegateHandle StartupHandle, void* Owner);
	void MaybeShowWelcomeDialog();
}
