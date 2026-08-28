// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;

/**
 * WBP 事件绑定的自动化构造通道（C4）。
 *
 * FDelegateEditorBinding（WBP->Bindings）在编辑器里只能通过 UMG 详情面板手工创建，
 * 命令行 / Python 没有公开入口，黑盒绑定场景无法真实执行。这里补一个与
 * Design2UMG.* 命令族风格一致的最小通道：枚举 + 添加（含函数图创建），
 * 控制台命令 Design2UMG.WidgetBindings 直接可用。
 */
namespace Design2UMGWidgetBindingTools
{
	/** 枚举 WBP 的编辑期事件绑定，返回 "WidgetName.PropertyName -> FunctionName" 文本列表。 */
	TArray<FString> GetEventBindingSummaries(const UWidgetBlueprint* WBP);

	/**
	 * 给指定 widget 添加事件绑定（FDelegateEditorBinding，Kind=Function）。
	 * - 校验 widget 存在、PropertyName 是 widget 类上的委托属性（单播或广播）；
	 * - FunctionName 对应的函数尚不存在时创建同名空函数图（保证编译期
	 *   FDelegateEditorBinding::IsBindingValid 能解析到函数，绑定不会被编译判无效）；
	 * - 同一 widget 同一属性只允许绑一个函数（引擎语义）：已有绑定时替换为新函数，
	 *   完全相同的绑定已存在时直接成功（幂等）。
	 * 只改内存状态，不编译不保存；持久化由调用方决定（控制台路径用
	 * FUMGGenerator::SaveWidgetBlueprint 编译 + 保存）。失败返回 false 并写 OutError。
	 */
	bool AddEventBinding(
		UWidgetBlueprint* WBP,
		const FString& WidgetName,
		const FString& PropertyName,
		const FString& FunctionName,
		FString& OutError);

	/** Design2UMG.WidgetBindings 控制台命令入口。 */
	void HandleConsoleCommand(const TArray<FString>& Args);
}
