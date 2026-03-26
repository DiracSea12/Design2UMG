// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UMGSchema.h"
#include "Extensions/UserWidgetExtension.h"
#include "Extensions/WidgetBlueprintGeneratedClassExtension.h"
#include "WidgetBlueprintExtension.h"
#include "Design2UMGPrototypeNavigationExtensions.generated.h"

class UUserWidget;
class UWidgetBlueprintGeneratedClass;
class UWidgetTree;
class FWidgetBlueprintCompilerContext;

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationClickProxy : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(
		class UDesign2UMGPrototypeNavigationUserWidgetExtension* InOwner,
		const FUMGPrototypeNavigationBinding& InBinding);

	UFUNCTION()
	void HandleClicked();

private:
	UPROPERTY(Transient)
	TObjectPtr<class UDesign2UMGPrototypeNavigationUserWidgetExtension> OwnerExtension;

	UPROPERTY(Transient)
	FUMGPrototypeNavigationBinding Binding;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationUserWidgetExtension : public UUserWidgetExtension
{
	GENERATED_BODY()

public:
	void Configure(const FUMGPrototypeNavigationData& InNavigationData);
	bool IsBound() const { return bBindingsInitialized; }
	void BindNavigation(UUserWidget* UserWidget);
	void HandleNavigationRequest(const FUMGPrototypeNavigationBinding& Binding);

#if WITH_EDITOR
	bool VerifyBindings(const UWidgetTree* WidgetTree) const;
#endif

private:
	bool TickActiveTransition(float DeltaTime);
	void StopActiveTransition(bool bSnapToFinalState);
	static float EvaluateEasingAlpha(float Alpha, const FString& EasingType);
	static FVector2D ResolveTransitionOffset(const class UWidgetSwitcher* Switcher, const FString& Direction);

	UPROPERTY(Transient)
	FUMGPrototypeNavigationData NavigationData;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDesign2UMGPrototypeNavigationClickProxy>> ClickProxies;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> BoundUserWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UWidgetSwitcher> BoundSwitcher;

	UPROPERTY(Transient)
	FUMGPrototypeNavigationBinding ActiveBinding;

	FTSTicker::FDelegateHandle TransitionTickerHandle;
	FVector2D TransitionBaseTranslation = FVector2D::ZeroVector;
	float TransitionBaseOpacity = 1.0f;
	float TransitionElapsedSeconds = 0.0f;
	bool bTransitionSwitchedTarget = false;

	bool bBindingsInitialized = false;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationClassExtension : public UWidgetBlueprintGeneratedClassExtension
{
	GENERATED_BODY()

public:
	void SetNavigationData(const FUMGPrototypeNavigationData& InNavigationData);

	virtual void Initialize(UUserWidget* UserWidget) override;
	virtual void Construct(UUserWidget* UserWidget) override;

#if WITH_EDITOR
	virtual void InitializeInEditor(UUserWidget* UserWidget) override;
	bool VerifyBindings(const UWidgetTree* WidgetTree) const;
#endif

private:
	void EnsureNavigationBound(UUserWidget* UserWidget);

	UPROPERTY()
	FUMGPrototypeNavigationData NavigationData;
};

UCLASS(Within = WidgetBlueprint)
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationBlueprintExtension : public UWidgetBlueprintExtension
{
	GENERATED_BODY()

public:
	void SetNavigationData(const FUMGPrototypeNavigationData& InNavigationData);

protected:
	virtual void HandleBeginCompilation(FWidgetBlueprintCompilerContext& InCreationContext) override;
	virtual void HandleFinishCompilingClass(UWidgetBlueprintGeneratedClass* Class) override;
	virtual bool HandleValidateGeneratedClass(UWidgetBlueprintGeneratedClass* Class) override;
	virtual void HandleEndCompilation() override;

private:
	FWidgetBlueprintCompilerContext* CompilerContext = nullptr;

	UPROPERTY()
	FUMGPrototypeNavigationData NavigationData;
};
