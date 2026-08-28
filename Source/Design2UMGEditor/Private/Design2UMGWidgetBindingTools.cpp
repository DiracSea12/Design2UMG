// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGWidgetBindingTools.h"

#include "Design2UMGLog.h"
#include "UMGGenerator.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGWidgetBindingTools
{
	namespace
	{
		bool FunctionExistsOnBlueprint(const UWidgetBlueprint* WBP, const FName FunctionName)
		{
			return (WBP->GeneratedClass && WBP->GeneratedClass->FindFunctionByName(FunctionName))
				|| (WBP->SkeletonGeneratedClass && WBP->SkeletonGeneratedClass->FindFunctionByName(FunctionName));
		}

		UWidgetBlueprint* LoadWidgetBlueprintByAssetPath(const FString& AssetPath, FString& OutError)
		{
			const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
			const FString ObjectPath = PackageName + TEXT(".") + FPaths::GetBaseFilename(PackageName);
			UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
			if (!WBP)
			{
				OutError = FString::Printf(TEXT("widget blueprint failed to load: %s"), *ObjectPath);
			}
			return WBP;
		}
	}

	TArray<FString> GetEventBindingSummaries(const UWidgetBlueprint* WBP)
	{
		TArray<FString> Summaries;
		if (!WBP)
		{
			return Summaries;
		}
		for (const FDelegateEditorBinding& Binding : WBP->Bindings)
		{
			Summaries.Add(FString::Printf(
				TEXT("%s.%s -> %s"),
				*Binding.ObjectName,
				*Binding.PropertyName.ToString(),
				*Binding.FunctionName.ToString()));
		}
		return Summaries;
	}

	bool AddEventBinding(
		UWidgetBlueprint* WBP,
		const FString& WidgetName,
		const FString& PropertyName,
		const FString& FunctionName,
		FString& OutError)
	{
		if (!WBP || !WBP->WidgetTree)
		{
			OutError = TEXT("widget blueprint or its widget tree is null");
			return false;
		}

		UWidget* Widget = WBP->WidgetTree->FindWidget(FName(*WidgetName));
		if (!Widget)
		{
			OutError = FString::Printf(TEXT("widget '%s' not found in %s"), *WidgetName, *WBP->GetName());
			return false;
		}

		// 事件绑定目标必须是 widget 类上的委托属性（事件广播或单播属性绑定）。
		const FName PropertyFName(*PropertyName);
		const bool bDelegateProperty = FindFProperty<FDelegateProperty>(Widget->GetClass(), PropertyFName) != nullptr
			|| FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(), PropertyFName) != nullptr;
		if (!bDelegateProperty)
		{
			OutError = FString::Printf(
				TEXT("'%s' is not a delegate property on widget class '%s'"),
				*PropertyName, *Widget->GetClass()->GetPathName());
			return false;
		}

		WBP->Modify();

		// 绑定的函数必须真实存在：编译期 IsBindingValid 按函数名解析，缺失的绑定
		// 不会转成运行时绑定（PIE 不生效）。尚不存在时创建同签名空函数图（void() 签名，
		// 与 OnClicked 等常见事件委托签名兼容）。函数名可能撞上既有成员（变量/图等）：
		// FindUniqueKismetName 给出的才是实际可用的图名，绑定与 guid 查询必须用同一个
		// 最终名字，否则绑定指向不存在的函数，编译期解析失败。
		FName FunctionFName(*FunctionName);
		if (!FunctionExistsOnBlueprint(WBP, FunctionFName))
		{
			FunctionFName = FBlueprintEditorUtils::FindUniqueKismetName(WBP, FunctionName);
			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
				WBP,
				FunctionFName,
				UEdGraph::StaticClass(),
				UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddFunctionGraph<UFunction>(
				WBP, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromObject=*/nullptr);
		}

		// 与编辑器 Bind 流程同构：先移除同 widget 同属性的旧绑定再落新条目。
		FDelegateEditorBinding Binding;
		Binding.ObjectName = Widget->GetFName().ToString();
		Binding.PropertyName = PropertyFName;
		Binding.FunctionName = FunctionFName;
		Binding.Kind = EBindingKind::Function;
		if (WBP->GeneratedClass)
		{
			UBlueprint::GetGuidFromClassByFieldName<UFunction>(
				WBP->GeneratedClass, FunctionFName, Binding.MemberGuid);
		}
		WBP->Bindings.RemoveAll([Binding](const FDelegateEditorBinding& Existing)
		{
			return Existing == Binding;
		});
		WBP->Bindings.Add(MoveTemp(Binding));
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
		return true;
	}

	void HandleConsoleCommand(const TArray<FString>& Args)
	{
		if (Args.Num() < 1 || (Args.Num() >= 2 && Args[1] == TEXT("--add") && Args.Num() < 5))
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("Usage: Design2UMG.WidgetBindings <WBPAssetPath> [--add <WidgetName> <PropertyName> <FunctionName>]"));
			return;
		}

		FString Error;
		UWidgetBlueprint* WBP = LoadWidgetBlueprintByAssetPath(Args[0], Error);
		if (!WBP)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] %s"), *Error);
			return;
		}

		if (Args.Num() >= 5 && Args[1] == TEXT("--add"))
		{
			if (!AddEventBinding(WBP, Args[2], Args[3], Args[4], Error))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("[FAIL] %s"), *Error);
				return;
			}
			// 编译 + 保存走生成链同一条路（含编译期绑定校验）；不写生成 manifest。
			if (!FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr))
			{
				UE_LOG(LogDesign2UMG, Error,
					TEXT("[FAIL] Binding '%s'.'%s' -> '%s' added but compile/save failed for %s"),
					*Args[2], *Args[3], *Args[4], *Args[0]);
				return;
			}
			UE_LOG(LogDesign2UMG, Log,
				TEXT("[OK] Binding added: '%s'.'%s' -> '%s' on %s"),
				*Args[2], *Args[3], *Args[4], *Args[0]);
		}

		const TArray<FString> Summaries = GetEventBindingSummaries(WBP);
		UE_LOG(LogDesign2UMG, Log, TEXT("Widget event bindings on %s (%d):"), *Args[0], Summaries.Num());
		for (const FString& Summary : Summaries)
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("  [Binding] %s"), *Summary);
		}
	}
}
