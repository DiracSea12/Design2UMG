// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Extensions/UserWidgetExtension.h"
#include "Extensions/WidgetBlueprintGeneratedClassExtension.h"
#include "UMGSchemaSourceExtensions.h"
#include "WidgetBlueprintExtension.h"
#include "Design2UMGPrototypeNavigationTagHelper.generated.h"

class UButton;
class UUserWidget;
class UWidget;
class UWidgetBlueprint;
class UWidgetBlueprintGeneratedClass;
class UWidgetSwitcher;
class FWidgetBlueprintCompilerContext;

USTRUCT()
struct FDesign2UMGPrototypeNavigationTagHelperBinding
{
	GENERATED_BODY()

	UPROPERTY()
	FName TriggerWidgetName = NAME_None;

	UPROPERTY()
	FString TargetPageId;

	bool IsValid() const
	{
		return TriggerWidgetName != NAME_None && !TargetPageId.IsEmpty();
	}
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationTagHelperClassExtension
	: public UWidgetBlueprintGeneratedClassExtension
{
	GENERATED_BODY()

public:
	void SetMetadata(const FUMGPrototypeNavigationData& InNavigationData);

	const FName& GetSwitcherWidgetName() const { return SwitcherWidgetName; }
	int32 GetInitialPageIndex() const { return InitialPageIndex; }
	int32 FindPageIndex(const FString& PageId) const;
	const TArray<FString>& GetPageIds() const { return PageIds; }
	const TArray<FDesign2UMGPrototypeNavigationTagHelperBinding>& GetBindings() const { return Bindings; }

#if WITH_EDITOR
	bool VerifyBindings(const class UWidgetTree* WidgetTree) const;
#endif

private:
	UPROPERTY()
	FName SwitcherWidgetName = NAME_None;

	UPROPERTY()
	int32 InitialPageIndex = 0;

	UPROPERTY()
	TArray<FString> PageIds;

	UPROPERTY()
	TArray<FDesign2UMGPrototypeNavigationTagHelperBinding> Bindings;
};

UCLASS(Within = WidgetBlueprint)
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension
	: public UWidgetBlueprintExtension
{
	GENERATED_BODY()

public:
	void SetMetadata(const FUMGPrototypeNavigationData& InNavigationData);

protected:
	virtual void HandleBeginCompilation(FWidgetBlueprintCompilerContext& InCreationContext) override;
	virtual void HandleFinishCompilingClass(UWidgetBlueprintGeneratedClass* Class) override;
	virtual bool HandleValidateGeneratedClass(UWidgetBlueprintGeneratedClass* Class) override;
	virtual void HandleEndCompilation() override;

private:
	bool HasAnyMetadata() const;

	FWidgetBlueprintCompilerContext* CompilerContext = nullptr;

	UPROPERTY()
	FUMGPrototypeNavigationData NavigationData;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationTagProxy : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(UWidgetSwitcher* InSwitcher, int32 InTargetIndex);

	UFUNCTION()
	void HandleClicked();

private:
	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> Switcher;

	UPROPERTY(Transient)
	int32 TargetIndex = INDEX_NONE;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationTagHelperExtension : public UUserWidgetExtension
{
	GENERATED_BODY()

public:
	void ResetBindings();
	void AddProxy(UDesign2UMGPrototypeNavigationTagProxy* Proxy);

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDesign2UMGPrototypeNavigationTagProxy>> Proxies;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationTagHelper : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Design2UMG|Prototype Navigation")
	static bool SetupNavigation(UWidgetSwitcher* Switcher, UUserWidget* UserWidget);
};

namespace Design2UMGPrototypeNavigationTagHelper
{
	void ConfigureMetadata(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGPrototypeNavigationData& NavigationData);
}
