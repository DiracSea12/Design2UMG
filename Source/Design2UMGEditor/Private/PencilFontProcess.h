// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

namespace DesignLocalFontImport { struct FFace; }

namespace PencilFontProcess
{
    /** Read a local WOFF2 face without instantiating or generating font output. */
    bool InspectFont(const FString& InputPath, DesignLocalFontImport::FFace& OutFace, FString& OutError);

    /** Convert one source font in its temporary directory; callers own all temporary files. */
    bool ConvertToStaticFont(const FString& InputPath, const FString& OutputPath, int32 SourceWeight,
                             int32 ExpectedWeight, FString& OutError);
}
