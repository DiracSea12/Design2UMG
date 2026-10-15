// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/NamedSlot.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

namespace Design2UMGComponentFixtures
{
	inline constexpr const TCHAR* InputClassPath = TEXT("/Game/Design2UMG/TestFixtures/WBP_ComponentInput.WBP_ComponentInput_C");
	inline constexpr const TCHAR* MappedClassPath = TEXT("/Game/Design2UMG/TestFixtures/WBP_MappedComponent.WBP_MappedComponent_C");

	inline UClass* CreateFixtureClass(const TCHAR* AssetName, bool bInputProperties)
	{
		const FString PackageName = FString(TEXT("/Game/Design2UMG/TestFixtures/")) + AssetName;
		const FString ObjectPath = PackageName + TEXT(".") + AssetName;
		if (FPackageName::DoesPackageExist(PackageName))
		{
			UWidgetBlueprint* Existing = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
			check(Existing);
			return Existing->GeneratedClass;
		}

		UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), CreatePackage(*PackageName), FName(AssetName), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		check(Blueprint);
		const auto AddVariable = [Blueprint](const TCHAR* Name, const FName Category,
			const FName SubCategory = NAME_None, const TCHAR* DefaultValue = TEXT(""))
		{
			FEdGraphPinType Type;
			Type.PinCategory = Category;
			Type.PinSubCategory = SubCategory;
			const bool bAdded = FBlueprintEditorUtils::AddMemberVariable(Blueprint, FName(Name), Type, DefaultValue);
			check(bAdded);
			FBlueprintEditorUtils::SetBlueprintVariableCategory(Blueprint, FName(Name), nullptr, FText::FromString(TEXT("Test")), true);
		};

		if (bInputProperties)
		{
			AddVariable(TEXT("TextValue"), UEdGraphSchema_K2::PC_Text);
			AddVariable(TEXT("BoolValue"), UEdGraphSchema_K2::PC_Boolean, NAME_None, TEXT("true"));
			AddVariable(TEXT("NameValue"), UEdGraphSchema_K2::PC_Name);
			AddVariable(TEXT("DoubleValue"), UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double, TEXT("0.0"));
			AddVariable(TEXT("FloatValue"), UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Float, TEXT("0.0"));
		}
		else
		{
			AddVariable(TEXT("Label"), UEdGraphSchema_K2::PC_Text);
			AddVariable(TEXT("EnabledFlag"), UEdGraphSchema_K2::PC_Boolean, NAME_None, TEXT("true"));
			AddVariable(TEXT("VariantName"), UEdGraphSchema_K2::PC_Name);
			AddVariable(TEXT("Value"), UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double, TEXT("0.0"));
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		check(Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings);
		const FString Filename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const bool bSaved = UPackage::SavePackage(Blueprint->GetOutermost(), Blueprint, *Filename, SaveArgs);
		check(bSaved);
		return Blueprint->GeneratedClass;
	}

	inline UClass* InputClass() { return CreateFixtureClass(TEXT("WBP_ComponentInput"), true); }
	inline UClass* MappedClass() { return CreateFixtureClass(TEXT("WBP_MappedComponent"), false); }

	inline UClass* NamedSlotHostClass()
	{
		UWidgetBlueprint* Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UWidgetBlueprint::StaticClass(), TEXT("NamedSlotHost")),
			BPTYPE_Normal, UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		check(Blueprint);
		Blueprint->WidgetTree->RootWidget = Blueprint->WidgetTree->ConstructWidget<UNamedSlot>(
			UNamedSlot::StaticClass(), TEXT("Content"));
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		check(Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings);
		return Blueprint->GeneratedClass;
	}

	inline UUserWidget* CastInput(UObject* Object)
	{
		return Object && Object->IsA(InputClass()) ? Cast<UUserWidget>(Object) : nullptr;
	}

	inline UUserWidget* CastMapped(UObject* Object)
	{
		return Object && Object->IsA(MappedClass()) ? Cast<UUserWidget>(Object) : nullptr;
	}

	template<typename PropertyType>
	auto Read(const UObject* Object, const TCHAR* Name)
	{
		const PropertyType* Property = FindFProperty<PropertyType>(Object->GetClass(), Name);
		check(Property);
		return Property->GetPropertyValue_InContainer(Object);
	}

	template<typename PropertyType, typename ValueType>
	void Write(UObject* Object, const TCHAR* Name, const ValueType& Value)
	{
		PropertyType* Property = FindFProperty<PropertyType>(Object->GetClass(), Name);
		check(Property);
		Property->SetPropertyValue_InContainer(Object, Value);
	}
}

namespace ComponentFixtures = Design2UMGComponentFixtures;
