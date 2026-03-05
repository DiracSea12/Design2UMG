// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

/**
 * 璁捐鏁版嵁婧愰€傞厤鍣ㄥ嚱鏁扮鍚? *
 * 杈撳叆: 鍘熷 JSON 瀛楃涓诧紙鍚勬暟鎹簮鑷繁鐨勬牸寮忥級
 * 杈撳嚭: FUMGWidgetData锛堢粺涓€涓棿琛ㄧず锛? */
using FDesignSourceConvertFunc = TFunction<bool(const FString& RawJson, FUMGWidgetData& OutData)>;

/**
 * 璁捐鏁版嵁婧愭敞鍐岃〃
 *
 * 鍚勯€傞厤鍣ㄥ湪妯″潡 StartupModule 涓皟鐢?Register() 娉ㄥ唽锛? * ShutdownModule 涓皟鐢?Unregister() 鍙嶆敞鍐屻€? */
class FDesignSourceRegistry
{
public:
	static void Register(FName SourceId, FDesignSourceConvertFunc Func);
	static void Unregister(FName SourceId);
	static bool Convert(FName SourceId, const FString& RawJson, FUMGWidgetData& OutData);
	static bool IsRegistered(FName SourceId);
	static TArray<FName> GetRegisteredSources();

private:
	static TMap<FName, FDesignSourceConvertFunc>& GetRegistry();
};

