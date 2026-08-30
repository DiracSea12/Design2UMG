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

		// The binding target must be a delegate property on the widget class (event multicast or unicast property binding).
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

		// The bound function must actually exist: compile-time IsBindingValid resolves by function name; a missing binding
		// never becomes a runtime binding (no effect in PIE). When it does not exist yet, an empty same-signature
		// function graph is created (void() signature, compatible with common event delegates like OnClicked).
		// The function name may collide with an existing member (variables, graphs, ...): the name FindUniqueKismetName
		// returns is the actually usable graph name; binding and guid lookup must use that same final name, or the binding points at a nonexistent function and fails compile-time resolution.
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

		// Isomorphic with the editor Bind flow: remove the old binding for the same widget+property first, then add the new entry.
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
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Error);
			return;
		}

		if (Args.Num() >= 5 && Args[1] == TEXT("--add"))
		{
			if (!AddEventBinding(WBP, Args[2], Args[3], Args[4], Error))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Error);
				return;
			}
			// Compile + save go through the same path as the generation chain (including compile-time binding validation); no generation manifest is written.
			if (!FUMGGenerator::SaveWidgetBlueprint(WBP, nullptr))
			{
				UE_LOG(LogDesign2UMG, Error,
					TEXT("Binding '%s'.'%s' -> '%s' added but compile/save failed for %s"),
					*Args[2], *Args[3], *Args[4], *Args[0]);
				return;
			}
			UE_LOG(LogDesign2UMG, Log,
				TEXT("Binding added: '%s'.'%s' -> '%s' on %s"),
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
