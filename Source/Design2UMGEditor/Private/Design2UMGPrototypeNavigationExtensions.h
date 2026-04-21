// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "UMGSchemaSourceExtensions.h"
#include "Extensions/UserWidgetExtension.h"
#include "Extensions/WidgetBlueprintGeneratedClassExtension.h"
#include "WidgetBlueprintExtension.h"
#include "Design2UMGPrototypeNavigationExtensions.generated.h"

class UUserWidget;
class UWidgetBlueprintGeneratedClass;
class UWidgetTree;
class UWidgetAnimation;
class FWidgetBlueprintCompilerContext;
struct FGeometry;
struct FPointerEvent;

UENUM()
enum class EDesign2UMGPrototypeGeneratedTransitionStage : uint8
{
	None,
	Outgoing,
	Incoming
};

USTRUCT()
struct FDesign2UMGPrototypeOverlayEntry
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	FString PageId;

	UPROPERTY(Transient)
	int32 OriginalIndex = INDEX_NONE;

	UPROPERTY(Transient)
	TObjectPtr<class UWidget> PageWidget = nullptr;
};

UCLASS()
class DESIGN2UMGEDITOR_API UDesign2UMGPrototypeNavigationTriggerProxy : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(
		class UDesign2UMGPrototypeNavigationUserWidgetExtension* InOwner,
		const FUMGPrototypeNavigationBinding& InBinding);

	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleHovered();

	UFUNCTION()
	void HandlePressed();

	FReply HandleDragDetected(const FGeometry& Geometry, const FPointerEvent& PointerEvent);

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
	bool ShouldCaptureKeyboardInput() const;
	bool HandleKeyboardInput(const FKey& Key, bool bIsKeyDown);
	virtual void BeginDestroy() override;

#if WITH_EDITOR
	bool VerifyBindings(const UWidgetTree* WidgetTree) const;
#endif

private:
	bool HasKeyboardBindings() const;
	bool HasKeyboardFocusForPrototypeInput() const;
	bool ExecutePrototypeProgram(const FUMGPrototypeNavigationBinding& Binding);
	bool ExecutePrototypeStepBinding(
		const FUMGPrototypeNavigationBinding& SourceBinding,
		const FUMGPrototypeNavigationStep& Step);
	bool PushOverlayPage(const FUMGPrototypeNavigationBinding& Binding);
	bool PopOverlayPage();
	void RestoreOverlayPage(const FDesign2UMGPrototypeOverlayEntry& Entry);
	void RestoreAllOverlayPages();
	class UWidget* ResolvePrototypePageWidget(const FString& PageId) const;
	bool TickActiveTransition(float DeltaTime);
	void StopActiveTransition(bool bSnapToFinalState);
	bool TryPlayGeneratedTransition(const FUMGPrototypeNavigationBinding& Binding);
	UWidgetAnimation* ResolveGeneratedTransitionAnimation(const FString& AnimationName) const;
	void StartGeneratedTransitionStage(UWidgetAnimation* Animation, EDesign2UMGPrototypeGeneratedTransitionStage Stage);
	void ClearGeneratedTransitionPlayback(bool bResetVisualState);
	UFUNCTION()
	void HandleGeneratedTransitionAnimationFinished();
	static float EvaluateEasingAlpha(float Alpha, const FString& EasingType);
	static FVector2D ResolveTransitionOffset(const class UWidgetSwitcher* Switcher, const FString& Direction);

	UPROPERTY(Transient)
	FUMGPrototypeNavigationData NavigationData;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDesign2UMGPrototypeNavigationTriggerProxy>> TriggerProxies;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> BoundUserWidget;

	UPROPERTY(Transient)
	TObjectPtr<class UWidgetSwitcher> BoundSwitcher;

	UPROPERTY(Transient)
	TObjectPtr<class UOverlay> BoundOverlayHost;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<class UWidget>> PrototypePageWidgets;

	UPROPERTY(Transient)
	TArray<FDesign2UMGPrototypeOverlayEntry> OverlayStack;

	UPROPERTY(Transient)
	FUMGPrototypeNavigationBinding ActiveBinding;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimation> ActiveGeneratedTransitionAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetAnimation> PendingIncomingAnimation;

	TMap<FString, FString> PrototypeVariableValues;
	TMap<FString, FString> PrototypeVariableModes;

	FTSTicker::FDelegateHandle TransitionTickerHandle;
	FVector2D TransitionBaseTranslation = FVector2D::ZeroVector;
	float TransitionBaseOpacity = 1.0f;
	float TransitionElapsedSeconds = 0.0f;
	bool bTransitionSwitchedTarget = false;
	EDesign2UMGPrototypeGeneratedTransitionStage GeneratedTransitionStage =
		EDesign2UMGPrototypeGeneratedTransitionStage::None;

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
