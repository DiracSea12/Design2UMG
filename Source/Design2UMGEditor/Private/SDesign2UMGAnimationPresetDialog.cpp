// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGAnimationPresets.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/MessageDialog.h"
#include "UObject/StrongObjectPtr.h"
#include "WidgetBlueprint.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "Design2UMGAnimationPresets"

namespace Design2UMGAnimationPresets
{
namespace
{
	class SAnimationPreview : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAnimationPreview) {} SLATE_ARGUMENT(UUserWidget*, Instance) SLATE_END_ARGS()
		void Construct(const FArguments& Args)
		{
			Instance.Reset(Args._Instance);
			const FVector2D Size = Instance->DesignTimeSize.ComponentMax(FVector2D(1, 1));
			ChildSlot[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
				[SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)[Instance->TakeWidget()]]];
		}
		~SAnimationPreview() { if (Instance.IsValid()) Instance->RemoveFromParent(); }
	private:
		TStrongObjectPtr<UUserWidget> Instance;
	};

	class SAnimationPresetDialog : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAnimationPresetDialog) {} SLATE_ARGUMENT(UWidgetBlueprint*, Blueprint) SLATE_END_ARGS()
		void Construct(const FArguments& Args)
		{
			Blueprint.Reset(Args._Blueprint);
			for (uint8 Index = 0; Index <= static_cast<uint8>(EPreset::ButtonPop); ++Index)
				Presets.Add(MakeShared<EPreset>(static_cast<EPreset>(Index)));
			RefreshTargets();
			TSharedRef<SVerticalBox> Fields = SNew(SVerticalBox);
			Fields->AddSlot().AutoHeight().Padding(0, 4)[SNew(STextBlock)
				.Text(FText::FromString(Blueprint->GetName()))];
			Fields->AddSlot().AutoHeight().Padding(0, 4)
			[
				SNew(SComboBox<TSharedPtr<EPreset>>).OptionsSource(&Presets).InitiallySelectedItem(Presets[0])
				.OnGenerateWidget_Lambda([](TSharedPtr<EPreset> Item) { return SNew(STextBlock).Text(GetPresetLabel(*Item)); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<EPreset> Item, ESelectInfo::Type)
				{
					if (!Item) return;
					Settings = FSettings(*Item);
					RefreshTargets();
					TargetPicker->RefreshOptions();
					TargetPicker->SetSelectedItem(Targets.IsEmpty() ? nullptr : Targets[0]);
				})
				[SNew(STextBlock).Text_Lambda([this] { return GetPresetLabel(Settings.Preset); })]
			];
			Fields->AddSlot().AutoHeight().Padding(0, 4)
			[
				SAssignNew(TargetPicker, SComboBox<TSharedPtr<FName>>).OptionsSource(&Targets)
				.InitiallySelectedItem(Targets.IsEmpty() ? nullptr : Targets[0])
				.OnGenerateWidget_Lambda([](TSharedPtr<FName> Item) { return SNew(STextBlock).Text(FText::FromName(*Item)); })
				.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Item, ESelectInfo::Type)
					{ Settings.TargetWidgetName = Item ? *Item : NAME_None; })
				[SNew(STextBlock).Text_Lambda([this] { return Settings.TargetWidgetName.IsNone()
					? LOCTEXT("NoTarget", "No native Button found in this Widget Blueprint") : FText::FromName(Settings.TargetWidgetName); })]
			];
			const auto AddNumber = [&](FText Label, float FSettings::* Member, TFunction<bool()> Visible)
			{
				Fields->AddSlot().AutoHeight().Padding(0, 4)
				[
					SNew(SHorizontalBox).Visibility_Lambda([Visible] { return Visible() ? EVisibility::Visible : EVisibility::Collapsed; })
					+ SHorizontalBox::Slot().FillWidth(1)[SNew(STextBlock).Text(Label)]
					+ SHorizontalBox::Slot().AutoWidth()
					[SNew(SNumericEntryBox<float>).MinDesiredValueWidth(110)
						.Value_Lambda([this, Member] { return Settings.*Member; })
						.OnValueChanged_Lambda([this, Member](float Value) { Settings.*Member = Value; })]
				];
			};
			const auto Always = [] { return true; };
			const auto Button = [this] { return !IsScreenPreset(Settings.Preset); };
			const auto Scale = [this] { return Settings.Preset == EPreset::ScreenScalePop || Settings.Preset == EPreset::ButtonScale
				|| Settings.Preset == EPreset::ButtonScaleAndLift || Settings.Preset == EPreset::ButtonPop; };
			const auto Distance = [this] { return (Settings.Preset >= EPreset::ScreenSlideLeft && Settings.Preset <= EPreset::ScreenSlideBottom)
				|| Settings.Preset == EPreset::ButtonLift || Settings.Preset == EPreset::ButtonScaleAndLift; };
			const auto Fade = [this] { return Settings.Preset == EPreset::ScreenFade || Settings.Preset == EPreset::ButtonFade; };
			AddNumber(LOCTEXT("Duration", "Duration (seconds)"), &FSettings::DurationSeconds, Always);
			AddNumber(LOCTEXT("Distance", "Entrance / hover distance"), &FSettings::Distance, Distance);
			AddNumber(LOCTEXT("Scale", "Entrance / hover scale"), &FSettings::Scale, Scale);
			AddNumber(LOCTEXT("Opacity", "Entrance / hover opacity"), &FSettings::Opacity, Fade);
			AddNumber(LOCTEXT("PressDistance", "Pressed distance"), &FSettings::PressDistance, [=] { return Button() && Distance(); });
			AddNumber(LOCTEXT("PressScale", "Pressed scale"), &FSettings::PressScale, [=] { return Button() && Scale(); });
			AddNumber(LOCTEXT("PressOpacity", "Pressed opacity"), &FSettings::PressOpacity, [=] { return Button() && Fade(); });
			AddNumber(LOCTEXT("Overshoot", "Pop overshoot"), &FSettings::Overshoot,
				[this] { return Settings.Preset == EPreset::ScreenScalePop || Settings.Preset == EPreset::ButtonPop; });
			AddNumber(LOCTEXT("HoverSpeed", "Hover playback speed"), &FSettings::HoverPlaybackSpeed, Button);
			AddNumber(LOCTEXT("PressSpeed", "Pressed playback speed"), &FSettings::PressPlaybackSpeed, Button);
			Fields->AddSlot().AutoHeight().Padding(0, 8)[SNew(STextBlock).AutoWrapText(true)
				.Text(LOCTEXT("Ownership", "Apply replaces this target's generated preset. Its curves remain editable in the Widget Blueprint animation timeline."))];
			Fields->AddSlot().AutoHeight().Padding(0, 6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return Status; })];
			Fields->AddSlot().AutoHeight().Padding(0, 6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SNew(SButton).Text(LOCTEXT("Apply", "Apply"))
					.IsEnabled_Lambda([this] { return !Settings.TargetWidgetName.IsNone(); })
					.OnClicked_Lambda([this]
					{
						TArray<FString> Warnings;
						if (Apply(Blueprint.Get(), Settings, Warnings)) Status = LOCTEXT("Applied", "Applied. Save the Widget Blueprint to keep the change.");
						else Status = FText::FromString(FString::Join(Warnings, TEXT("\n")));
						return FReply::Handled();
					})]
				+ SHorizontalBox::Slot().AutoWidth().Padding(8, 0)[SNew(SButton).Text(LOCTEXT("Preview", "Preview applied animation"))
					.OnClicked_Lambda([this] { Preview(); return FReply::Handled(); })]
			];
			ChildSlot.Padding(16)[SNew(SScrollBox) + SScrollBox::Slot()[Fields]];
		}
	private:
		void RefreshTargets()
		{
			Targets.Reset();
			if (IsScreenPreset(Settings.Preset))
			{
				if (Blueprint->WidgetTree->RootWidget) Targets.Add(MakeShared<FName>(Blueprint->WidgetTree->RootWidget->GetFName()));
			}
			else Blueprint->WidgetTree->ForEachWidget([this](UWidget* Widget)
			{
				if (Cast<UButton>(Widget)) Targets.Add(MakeShared<FName>(Widget->GetFName()));
			});
			Settings.TargetWidgetName = Targets.IsEmpty() ? NAME_None : *Targets[0];
		}
		void Preview()
		{
			UWidgetBlueprint* Asset = Blueprint.Get();
			FKismetEditorUtilities::CompileBlueprint(Asset);
			if ((Asset->Status != BS_UpToDate && Asset->Status != BS_UpToDateWithWarnings) || !GEditor) return;
			UClass* WidgetClass = Asset->GeneratedClass.Get();
			if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass())) return;
			UUserWidget* Instance = CreateWidget<UUserWidget>(GEditor->GetEditorWorldContext().World(), WidgetClass);
			if (!Instance) { Status = LOCTEXT("PreviewFailed", "Could not create an animation preview."); return; }
			FSlateApplication::Get().AddWindow(SNew(SWindow).Title(LOCTEXT("PreviewTitle", "Animation preview"))
				.ClientSize(FVector2D(900, 650))[SNew(SAnimationPreview).Instance(Instance)]);
		}
		TStrongObjectPtr<UWidgetBlueprint> Blueprint;
		FSettings Settings;
		FText Status;
		TArray<TSharedPtr<EPreset>> Presets;
		TArray<TSharedPtr<FName>> Targets;
		TSharedPtr<SComboBox<TSharedPtr<FName>>> TargetPicker;
	};
}

void OpenDialog()
{
	TArray<FAssetData> Selected;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().GetSelectedAssets(Selected);
	UWidgetBlueprint* Blueprint = Selected.Num() == 1 ? Cast<UWidgetBlueprint>(Selected[0].GetAsset()) : nullptr;
	if (!Blueprint || !Blueprint->WidgetTree)
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("SelectAsset", "Select one Widget Blueprint in the Content Browser, then open Animation Presets."));
		return;
	}
	FSlateApplication::Get().AddWindow(SNew(SWindow).Title(LOCTEXT("DialogTitle", "Animation presets"))
		.ClientSize(FVector2D(560, 580))[SNew(SAnimationPresetDialog).Blueprint(Blueprint)]);
}
}

#undef LOCTEXT_NAMESPACE
