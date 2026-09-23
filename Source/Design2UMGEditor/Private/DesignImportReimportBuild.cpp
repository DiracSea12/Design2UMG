// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportReimportInternal.h"

#include "Design2UMGComponentBlueprints.h"
#include "UMGWidgetUtils.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelSlot.h"
#include "Components/PanelWidget.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectHash.h"
#include "WidgetBlueprint.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace DesignImportPipeline
{
	namespace
	{
		// Preparation uses the real tree's naming rules, but publishes no ownership records
		// until attachment succeeds. Failed recursive builds can leave detached children too.
		class FReimportWidgetBuild
		{
		public:
			explicit FReimportWidgetBuild(const FReimportApplySharedState& InShared)
				: State(*InShared.MappingBuildState), Shared(InShared)
			{
				TArray<UObject*> Objects;
#if UE_VERSION_OLDER_THAN(5, 8, 0)
				GetObjectsWithOuter(Shared.WidgetTree, Objects, false);
#else
				GetObjectsWithOuter(Shared.WidgetTree, Objects, EGetObjectsFlags::None);
#endif
				ExistingObjects.Append(Objects);
			}

			~FReimportWidgetBuild()
			{
				if (bCommitted) return;
				const auto& Diagnostics = State.GetDiagnostics();
				for (int32 Index = Shared.MappingBuildState->GetDiagnostics().Num(); Index < Diagnostics.Num(); ++Index)
				{
					Shared.MappingBuildState->AddDiagnostic(Diagnostics[Index].Kind, Diagnostics[Index].Message);
				}
				DiscardDetachedReimportWidgets(Shared.WidgetTree, ExistingObjects, *Shared.Diagnostics);
			}

			UWidget* Create(const FUMGWidgetNode& Node)
			{
				UWidget* Widget = FUMGWidgetUtils::CreateWidgetRecursive(Shared.WidgetTree, Node, nullptr,
					nullptr, &Shared.DesignSize, &State, nullptr, Shared.GenerationSettings, Shared.Budget);
				if (!Widget)
				{
					AddReimportDiagnostic(*Shared.Diagnostics, ReimportDiagnosticKinds::PrepareFailed,
						FString::Printf(TEXT("Could not build node '%s' (%s); existing subtree preserved"),
							*Node.Name, *GetDesignNodeId(Node)));
				}
				return Widget;
			}

			void Commit()
			{
				*Shared.MappingBuildState = MoveTemp(State);
				bCommitted = true;
			}

			FDesign2UMGComponentMappingBuildState State;

		private:
			const FReimportApplySharedState& Shared;
			TSet<UObject*> ExistingObjects;
			bool bCommitted = false;
		};

		void ReportAttachFailure(const FUMGWidgetNode& Node, const FReimportApplySharedState& Shared)
		{
			AddReimportDiagnostic(*Shared.Diagnostics, ReimportDiagnosticKinds::AttachFailed,
				FString::Printf(TEXT("Could not attach node '%s' (%s); original parent, slot and subtree preserved"),
					*Node.Name, *GetDesignNodeId(Node)));
		}
	}

	bool DiscardDetachedReimportWidgets(UWidgetTree* WidgetTree, const TSet<UObject*>& ExistingObjects,
		TArray<FDesign2UMGMappingDiagnostic>& Diagnostics)
	{
		bool bDiscarded = true;
		TSet<UWidget*> Attached;
		WidgetTree->ForEachWidget([&](UWidget* Widget) { Attached.Add(Widget); });
		TArray<UObject*> Objects;
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		GetObjectsWithOuter(WidgetTree, Objects, false);
#else
		GetObjectsWithOuter(WidgetTree, Objects, EGetObjectsFlags::None);
#endif
		for (UObject* Object : Objects)
		{
			UWidget* Widget = Cast<UWidget>(Object);
			if (Widget && !ExistingObjects.Contains(Object) && !Attached.Contains(Widget))
			{
				Widget->Slot = nullptr;
				if (!Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional))
				{
					bDiscarded = false;
					AddReimportDiagnostic(Diagnostics, ReimportDiagnosticKinds::ApplyFailed,
						TEXT("Could not discard uncommitted widget: ") + Widget->GetName());
				}
			}
		}
		return bDiscarded;
	}

	UWidget* CreateReimportWidget(const FUMGWidgetNode& Node, UPanelWidget* Parent,
		const FReimportApplySharedState& Shared)
	{
		FReimportWidgetBuild Build(Shared);
		UWidget* Widget = Build.Create(Node);
		if (!Widget) return nullptr;
		if (Parent) Parent->Modify();
		if (!Parent || !Parent->AddChild(Widget))
		{
			ReportAttachFailure(Node, Shared);
			return nullptr;
		}
		ApplySlotForNode(Widget, Node, Shared);
		Build.State.RecordCreatedWidget(Node, Widget, Parent);
		Build.Commit();
		return Widget;
	}

	UWidget* ReplaceReimportWidget(const FUMGWidgetNode& Node, UWidget* OldWidget,
		const FReimportApplySharedState& Shared, TSet<FString>& UpdatedD2UOwned)
	{
		FReimportWidgetBuild Build(Shared);
		UWidget* Widget = Build.Create(Node);
		if (!Widget) return nullptr;
		UPanelWidget* Parent = OldWidget->GetParent();
		if (Parent) Parent->Modify();
		OldWidget->Modify();
		if (OldWidget->Slot) OldWidget->Slot->Modify();
		if (!Parent || !Parent->ReplaceChild(OldWidget, Widget))
		{
			ReportAttachFailure(Node, Shared);
			return nullptr;
		}
		// ReplaceChild reuses the slot but does not clear its former content's back-pointer.
		// Clear it before deletion, or the editor delete flow would remove the replacement.
		OldWidget->Slot = nullptr;
		DeleteD2UOwnedSubtreeSilently(Shared.WBP, OldWidget, UpdatedD2UOwned);
		ApplySlotForNode(Widget, Node, Shared);
		Build.State.RecordCreatedWidget(Node, Widget, Parent);
		Build.Commit();
		return Widget;
	}

	bool MoveReimportWidget(UWidget* Widget, UPanelWidget* Target, const FUMGWidgetNode& Node,
		const FReimportApplySharedState& Shared)
	{
		// A node cannot be moved into its own subtree, even while other planned moves are pending.
		for (UWidget* Ancestor = Target; Ancestor; Ancestor = Ancestor->GetParent())
		{
			if (Ancestor == Widget)
			{
				ReportAttachFailure(Node, Shared);
				return false;
			}
		}
		if (UPanelWidget* Parent = Widget->GetParent()) Parent->Modify();
		Widget->Modify();
		if (Widget->Slot) Widget->Slot->Modify();
		Target->Modify();
		// AddChild checks target capacity before detaching Content from its original parent.
		if (!Target->AddChild(Widget))
		{
			ReportAttachFailure(Node, Shared);
			return false;
		}
		ApplySlotForNode(Widget, Node, Shared);
		return true;
	}

	UWidget* RefreshAutomaticInstance(const FUMGWidgetNode& Node, UWidget* Widget,
		const FReimportApplySharedState& Shared)
	{
		auto& Context = *Shared.GenerationSettings->ComponentBlueprints;
		UUserWidget* Instance = Cast<UUserWidget>(Widget);
		const auto* Component = Context.Components.Find(Context.ResolveKey(Node));
		auto Fail = [&](const FString& Reason) -> UWidget*
		{
			Context.Error = Reason + TEXT(": ") + Node.Name;
			AddReimportDiagnostic(*Shared.Diagnostics, ReimportDiagnosticKinds::PrepareFailed, Context.Error);
			return nullptr;
		};
		if (!Instance || !Component) return Fail(TEXT("Automatic instance no longer matches generated widget"));

		TArray<FName> Slots;
		Instance->GetSlotNames(Slots);
		TSet<FString> UpdatedOwned = *Shared.OwnedNames;
		FString ConflictReason;
		for (const FName Slot : Slots)
		{
			if (UWidget* Content = Instance->GetContentForSlot(Slot))
			{
				if (!IsSubtreePurelyD2UOwned(Shared.WBP, Content, UpdatedOwned, ConflictReason))
					return Fail(TEXT("Instance override contains user changes; ") + ConflictReason);
			}
		}
		if (Instance->GetClass()->GetPathName() != Component->ClassPath)
		{
			if (!Instance->GetParent() || !IsSubtreePurelyD2UOwned(Shared.WBP, Instance, UpdatedOwned, ConflictReason))
				return Fail(TEXT("Component swap conflicts with user content; ") + ConflictReason);
			return ReplaceReimportWidget(Node, Instance, Shared, UpdatedOwned);
		}

		FReimportWidgetBuild Build(Shared);
		TStrongObjectPtr<UUserWidget> Prepared(NewObject<UUserWidget>(GetTransientPackage(), Instance->GetClass()));
		if (!Context.ApplyInstance(Prepared.Get(), Shared.WidgetTree, Node, *Shared.GenerationSettings,
			nullptr, &Shared.DesignSize, &Build.State, Shared.Budget))
			return Fail(TEXT("Could not prepare instance overrides; existing content preserved"));

		TArray<UWidget*> OldContents;
		for (const FName Slot : Slots) OldContents.Add(Instance->GetContentForSlot(Slot));
		Instance->Modify();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			UWidget* Content = Prepared->GetContentForSlot(Slots[Index]);
			Instance->SetContentForSlot(Slots[Index], Content);
			if (Instance->GetContentForSlot(Slots[Index]) != Content)
			{
				bool bRestored = true;
				for (int32 RestoreIndex = 0; RestoreIndex <= Index; ++RestoreIndex)
				{
					Instance->SetContentForSlot(Slots[RestoreIndex], OldContents[RestoreIndex]);
					bRestored &= Instance->GetContentForSlot(Slots[RestoreIndex]) == OldContents[RestoreIndex];
				}
				Context.Error = bRestored ? TEXT("Could not attach instance overrides; original slots restored")
					: TEXT("Could not attach instance overrides; slot restoration incomplete");
				AddReimportDiagnostic(*Shared.Diagnostics, ReimportDiagnosticKinds::AttachFailed, Context.Error + TEXT(": ") + Node.Name);
				return nullptr;
			}
		}
		for (UWidget* Content : OldContents)
		{
			if (Content) DeleteD2UOwnedSubtreeSilently(Shared.WBP, Content, UpdatedOwned);
		}
		Build.Commit();
		return Instance;
	}
}
