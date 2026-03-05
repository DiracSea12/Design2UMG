// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/SlateWrapperTypes.h"
#include "UMGSchema.h"

class UWidget;
class UPanelSlot;
class UPanelWidget;
class UWidgetTree;
class UWidgetBlueprint;
class UTexture2D;

/**
 * UMG Widget 宸ュ叿绫?- 鍏变韩鐨?Widget 鍒涘缓鍜屽睘鎬у簲鐢ㄩ€昏緫
 */
class FUMGWidgetUtils
{
public:
	// ========== Widget 鍒涘缓 ==========

	/** 閫掑綊鍒涘缓 Widget 鏍戯紙TextureMap 闈炵┖鏃跺皢鍖归厤鐨勭汗鐞嗙粦瀹氬埌 Image 鎺т欢锛?*/
	static UWidget* CreateWidgetRecursive(UWidgetTree* WidgetTree, const FUMGWidgetNode& Node, UPanelWidget* ParentWidget = nullptr, const TMap<FString, UTexture2D*>* TextureMap = nullptr);

	/** 搴旂敤Fixer淇Bounds锛堝湪CreateWidgetRecursive涔嬪墠璋冪敤锛?
	 * @param RootNode Widget鏍戞牴鑺傜偣
	 * @param DesignSize 璁捐灏哄锛圲MG鐩爣锛?
	 */
	static void ApplyFixer(FUMGWidgetNode& RootNode, const FIntPoint& DesignSize);

	/** Fixer/LayoutCalculator 浣跨敤鐨勫疄闄?DesignSize锛堝彲鑳借璁捐绋垮昂瀵歌鐩栵級 */
	static FIntPoint EffectiveDesignSize;

	/** 浠嶹idget鏍戣绠楁墍闇€鐨勮璁″昂瀵革紙閬嶅巻鎵€鏈夊瓙鑺傜偣锛屾壘鍑烘渶澶ц竟鐣岋級 */
	static FVector2D CalculateDesignSize(const FUMGWidgetNode& RootNode, FVector2D DefaultSize = FVector2D(1920, 1080));

	// ========== Widget 绫诲瀷瑙ｆ瀽 ==========

	/** 鏍规嵁绫诲瀷鍚嶇О鑾峰彇 Widget 绫?*/
	static UClass* ResolveWidgetClass(const FString& TypeName);

	// ========== 棰滆壊瑙ｆ瀽 ==========

	/** 瑙ｆ瀽棰滆壊瀛楃涓?- 鏀寔 #RRGGBB 鍜岄鑹插悕绉?*/
	static FLinearColor ParseColor(const FString& ColorStr);

	// ========== 瀵归綈鏂瑰紡瑙ｆ瀽 ==========

	static EHorizontalAlignment ParseHAlign(const FString& Str);
	static EVerticalAlignment ParseVAlign(const FString& Str);
	static ESlateVisibility ParseVisibility(const FString& Str);

	// ========== 鍑犱綍瑙ｆ瀽 ==========

	static FVector2D ParseVector2D(const FString& Str, FVector2D Default = FVector2D::ZeroVector);
	static FMargin ParseMargin(const FString& Str);

	/** FMargin 杩戜技鐩哥瓑姣旇緝 */
	static bool IsNearlyEqualMargin(const FMargin& A, const FMargin& B)
	{
		return FMath::IsNearlyEqual(A.Left, B.Left) &&
			FMath::IsNearlyEqual(A.Top, B.Top) &&
			FMath::IsNearlyEqual(A.Right, B.Right) &&
			FMath::IsNearlyEqual(A.Bottom, B.Bottom);
	}

	// ========== Slot 灞炴€у簲鐢?==========

	/** 搴旂敤 Slot 灞炴€у埌鍚勭绫诲瀷鐨?Slot */
	static void ApplySlotProperties(UPanelSlot* Slot, const FUMGSlotData& SlotData);

	// ========== Widget 灞炴€у簲鐢?==========

	/** 搴旂敤绫诲瀷鐗瑰畾鐨?Widget 灞炴€э紙legacy string map锛?*/
	static void ApplyWidgetProperties(UWidget* Widget, const TMap<FString, FString>& Properties);

	/** 搴旂敤绫诲瀷鐗瑰畾鐨?Widget 灞炴€э紙typed 浼樺厛锛宭egacy 鍥為€€锛?*/
	static void ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node);

	// ========== Widget 娓叉煋 ==========

	/** 灏?WidgetBlueprint 娓叉煋涓?PNG 鍥剧墖鏁版嵁锛堥渶瑕佸凡缂栬瘧鐨?GeneratedClass锛?*/
	static bool RenderWidgetToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize);

	/** 鐩存帴浠?WidgetTree 娓叉煋涓?PNG锛堢紪璇戝墠璋冪敤锛宼ransient 绾圭悊鏁版嵁浠嶆湁鏁堬級 */
	static bool RenderWidgetTreeToPng(UWidgetBlueprint* WidgetBP, const FIntPoint& RenderSize, TArray<uint8>& OutPngData, FIntPoint& OutSize);

	// ========== Node ID Metadata ==========

	/** Widget 鈫?.pen 鑺傜偣 ID 鏄犲皠锛堝閲忔洿鏂板熀纭€璁炬柦锛?*/
	static TMap<TWeakObjectPtr<UWidget>, FString> NodeIdMap;

	/** 鏌ヨ Widget 瀵瑰簲鐨?.pen 鑺傜偣 ID锛屾湭鎵惧埌杩斿洖绌?*/
	static FString FindNodeId(const UWidget* Widget);

	/** 娓呯┖鏄犲皠琛紙姣忔瀵煎叆鍓嶈皟鐢級 */
	static void ClearNodeIdMap() { NodeIdMap.Empty(); }

	// ========== Widget 鍚嶇О澶勭悊 ==========

	/** 娓呯悊 Widget 鍚嶇О锛岀Щ闄ら潪娉曞瓧绗?*/
	static FName SanitizeWidgetName(const FString& InName, const FString& FallbackPrefix);

private:
	/** 搴旂敤閫氱敤 Widget 灞炴€э紙鍐呴儴浣跨敤锛?*/
	static void ApplyCommonProperties(UWidget* Widget, const TMap<FString, FString>& Properties);
};

