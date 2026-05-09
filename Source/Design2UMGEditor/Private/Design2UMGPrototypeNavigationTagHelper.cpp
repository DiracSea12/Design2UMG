// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGPrototypeNavigationTagHelper.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/WidgetSwitcher.h"
#include "UMGWidgetUtils.h"
#include "WidgetBlueprint.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(Design2UMGPrototypeNavigationTagHelper)

namespace
{
	FName MakeRuntimeWidgetName(const FString& RawName, const FString& FallbackPrefix)
	{
		return FUMGWidgetUtils::SanitizeWidgetName(RawName, FallbackPrefix);
	}

	bool IsSupportedTagHelperBinding(const FUMGPrototypeNavigationBinding& Binding)
	{
		return Binding.IsValid()
			&& Binding.UsesClickTrigger()
			&& Binding.Action == EUMGPrototypeNavigationAction::Navigate
			&& !Binding.TargetPageId.IsEmpty();
	}

	bool HasSupportedTagHelperBinding(const FUMGPrototypeNavigationData& NavigationData)
	{
		if (!NavigationData.IsValid())
		{
			return false;
		}

		for (const FUMGPrototypeNavigationBinding& Binding : NavigationData.ClickBindings)
		{
			if (IsSupportedTagHelperBinding(Binding))
			{
				return true;
			}
		}

		return false;
	}
}

void UDesign2UMGPrototypeNavigationTagHelperClassExtension::SetMetadata(
	const FUMGPrototypeNavigationData& InNavigationData)
{
	SwitcherWidgetName = MakeRuntimeWidgetName(
		InNavigationData.SwitcherWidgetName,
		TEXT("PrototypeSwitcher"));
	InitialPageIndex = InNavigationData.InitialPageIndex;
	PageIds = InNavigationData.PageIds;
	Bindings.Reset();

	for (const FUMGPrototypeNavigationBinding& Binding : InNavigationData.ClickBindings)
	{
		if (!IsSupportedTagHelperBinding(Binding))
		{
			continue;
		}

		FDesign2UMGPrototypeNavigationTagHelperBinding HelperBinding;
		HelperBinding.TriggerWidgetName = MakeRuntimeWidgetName(
			Binding.TriggerWidgetName,
			TEXT("PrototypeTrigger"));
		HelperBinding.TargetPageId = Binding.TargetPageId;
		if (HelperBinding.IsValid())
		{
			Bindings.Add(HelperBinding);
		}
	}
}

int32 UDesign2UMGPrototypeNavigationTagHelperClassExtension::FindPageIndex(
	const FString& PageId) const
{
	return PageIds.IndexOfByKey(PageId);
}

#if WITH_EDITOR
bool UDesign2UMGPrototypeNavigationTagHelperClassExtension::VerifyBindings(
	const UWidgetTree* WidgetTree) const
{
	if (!WidgetTree || SwitcherWidgetName == NAME_None || Bindings.Num() == 0)
	{
		return false;
	}

	if (!Cast<UWidgetSwitcher>(WidgetTree->FindWidget(SwitcherWidgetName)))
	{
		return false;
	}

	for (const FDesign2UMGPrototypeNavigationTagHelperBinding& Binding : Bindings)
	{
		if (!Binding.IsValid()
			|| FindPageIndex(Binding.TargetPageId) == INDEX_NONE
			|| !Cast<UButton>(WidgetTree->FindWidget(Binding.TriggerWidgetName)))
		{
			return false;
		}
	}

	return true;
}
#endif

void UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension::SetMetadata(
	const FUMGPrototypeNavigationData& InNavigationData)
{
	NavigationData = InNavigationData;
}

void UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension::HandleBeginCompilation(
	FWidgetBlueprintCompilerContext& InCreationContext)
{
	Super::HandleBeginCompilation(InCreationContext);
	CompilerContext = &InCreationContext;
}

void UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension::HandleFinishCompilingClass(
	UWidgetBlueprintGeneratedClass* Class)
{
	Super::HandleFinishCompilingClass(Class);

	if (!CompilerContext || !HasAnyMetadata() || !Class)
	{
		return;
	}

	UDesign2UMGPrototypeNavigationTagHelperClassExtension* NewExtension =
		NewObject<UDesign2UMGPrototypeNavigationTagHelperClassExtension>(Class);
	NewExtension->SetMetadata(NavigationData);
	CompilerContext->AddExtension(Class, NewExtension);
}

bool UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension::HandleValidateGeneratedClass(
	UWidgetBlueprintGeneratedClass* Class)
{
	if (!HasAnyMetadata() || !Class)
	{
		return true;
	}

	const UDesign2UMGPrototypeNavigationTagHelperClassExtension* RuntimeExtension =
		Class->GetExtension<UDesign2UMGPrototypeNavigationTagHelperClassExtension>();
	return RuntimeExtension ? RuntimeExtension->VerifyBindings(Class->GetWidgetTreeArchetype()) : false;
}

void UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension::HandleEndCompilation()
{
	CompilerContext = nullptr;
}

bool UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension::HasAnyMetadata() const
{
	return HasSupportedTagHelperBinding(NavigationData);
}

void UDesign2UMGPrototypeNavigationTagProxy::Initialize(
	UWidgetSwitcher* InSwitcher,
	const int32 InTargetIndex)
{
	Switcher = InSwitcher;
	TargetIndex = InTargetIndex;
}

void UDesign2UMGPrototypeNavigationTagProxy::HandleClicked()
{
	if (Switcher && TargetIndex >= 0 && TargetIndex < Switcher->GetNumWidgets())
	{
		Switcher->SetActiveWidgetIndex(TargetIndex);
	}
}

void UDesign2UMGPrototypeNavigationTagHelperExtension::ResetBindings()
{
	Proxies.Reset();
}

void UDesign2UMGPrototypeNavigationTagHelperExtension::AddProxy(
	UDesign2UMGPrototypeNavigationTagProxy* Proxy)
{
	if (Proxy)
	{
		Proxies.Add(Proxy);
	}
}

bool UDesign2UMGPrototypeNavigationTagHelper::SetupNavigation(
	UWidgetSwitcher* Switcher,
	UUserWidget* UserWidget)
{
	if (!Switcher || !UserWidget || !UserWidget->WidgetTree)
	{
		return false;
	}

	UWidgetBlueprintGeneratedClass* GeneratedClass =
		Cast<UWidgetBlueprintGeneratedClass>(UserWidget->GetClass());
	const UDesign2UMGPrototypeNavigationTagHelperClassExtension* Metadata =
		GeneratedClass ? GeneratedClass->GetExtension<UDesign2UMGPrototypeNavigationTagHelperClassExtension>() : nullptr;
	if (!Metadata || Metadata->GetSwitcherWidgetName() != Switcher->GetFName())
	{
		return false;
	}

	const int32 InitialIndex = Metadata->GetInitialPageIndex();
	if (InitialIndex >= 0 && InitialIndex < Switcher->GetNumWidgets())
	{
		Switcher->SetActiveWidgetIndex(InitialIndex);
	}

	UDesign2UMGPrototypeNavigationTagHelperExtension* Extension =
		UserWidget->GetExtension<UDesign2UMGPrototypeNavigationTagHelperExtension>();
	if (!Extension)
	{
		Extension = UserWidget->AddExtension<UDesign2UMGPrototypeNavigationTagHelperExtension>();
	}
	if (!Extension)
	{
		return false;
	}

	Extension->ResetBindings();
	int32 BoundCount = 0;
	for (const FDesign2UMGPrototypeNavigationTagHelperBinding& Binding : Metadata->GetBindings())
	{
		const int32 TargetIndex = Metadata->FindPageIndex(Binding.TargetPageId);
		UButton* Button = Cast<UButton>(UserWidget->WidgetTree->FindWidget(Binding.TriggerWidgetName));
		if (!Button || TargetIndex == INDEX_NONE)
		{
			continue;
		}

		UDesign2UMGPrototypeNavigationTagProxy* Proxy =
			NewObject<UDesign2UMGPrototypeNavigationTagProxy>(Extension);
		Proxy->Initialize(Switcher, TargetIndex);
		Button->OnClicked.AddUniqueDynamic(
			Proxy,
			&UDesign2UMGPrototypeNavigationTagProxy::HandleClicked);
		Extension->AddProxy(Proxy);
		++BoundCount;
	}

	return BoundCount > 0;
}

namespace Design2UMGPrototypeNavigationTagHelper
{
	void ConfigureMetadata(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGPrototypeNavigationData& NavigationData)
	{
		if (!WidgetBlueprint)
		{
			return;
		}

		if (HasSupportedTagHelperBinding(NavigationData))
		{
			UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension* Extension =
				UWidgetBlueprintExtension::RequestExtension<UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension>(
					WidgetBlueprint);
			Extension->SetMetadata(NavigationData);
			return;
		}

		if (UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension* ExistingExtension =
			UWidgetBlueprintExtension::GetExtension<UDesign2UMGPrototypeNavigationTagHelperBlueprintExtension>(
				WidgetBlueprint))
		{
			WidgetBlueprint->RemoveExtension(ExistingExtension);
		}
	}
}
