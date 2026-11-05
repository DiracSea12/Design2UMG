// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGAnimationPresets.h"
#include "UMGPrototypeAnimationBuilder.h"
#include "Design2UMGWidgetMetadata.h"
#include "GeneratedAssetLifecycle.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Components/Button.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "Misc/SecureHash.h"
#include "Misc/Change.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectHash.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "WidgetBlueprint.h"

#define LOCTEXT_NAMESPACE "Design2UMGAnimationPresets"

namespace Design2UMGAnimationPresets
{
namespace
{
	const TCHAR* MetadataKey = TEXT("Design2UMG.AnimationPresets");
	const TCHAR* TargetIdsKey = TEXT("Design2UMG.AnimationTargetIds");
	const TCHAR* AnimationOwnerKey = TEXT("Design2UMG.AnimationPresetOwned");
	class FMetadataChange final : public FSwapChange
	{
	public:
		FMetadataChange(FName InKey, FString InValue) : Key(InKey), Value(MoveTemp(InValue)) {}
		TUniquePtr<FChange> Execute(UObject* Object) override
		{
			auto& Metadata = Design2UMGWidgetMetadata::PackageMetadata(Object);
			TUniquePtr<FChange> Inverse = MakeUnique<FMetadataChange>(Key, Metadata.GetValue(Object, Key));
			Metadata.SetValue(Object, Key, *Value);
			return Inverse;
		}
		FString ToString() const override { return TEXT("Animation preset editor metadata"); }
	private:
		FName Key;
		FString Value;
	};

	void SetMetadata(UObject* Object, const TCHAR* Key, const FString& Value)
	{
		auto& Metadata = Design2UMGWidgetMetadata::PackageMetadata(Object);
		const FString Previous = Metadata.GetValue(Object, Key);
		if (Previous == Value) return;
		if (GUndo && !GIsTransacting)
		{
			// Snapshot the object first; a lone custom record stops later Modify calls from saving its state.
			Object->Modify();
			GUndo->StoreUndo(Object, MakeUnique<FMetadataChange>(FName(Key), Previous));
		}
		Metadata.SetValue(Object, Key, *Value);
	}
	const TCHAR* PresetNames[] = {
		TEXT("ScreenFade"), TEXT("ScreenSlideLeft"), TEXT("ScreenSlideRight"), TEXT("ScreenSlideTop"),
		TEXT("ScreenSlideBottom"), TEXT("ScreenScalePop"), TEXT("ButtonScale"), TEXT("ButtonLift"),
		TEXT("ButtonFade"), TEXT("ButtonScaleAndLift"), TEXT("ButtonPop")};

	FName AnimationName(const FSettings& Settings, const TCHAR* Use)
	{
		const FString Identity = Settings.TargetNodeId.IsEmpty() ? Settings.TargetWidgetName.ToString()
			: FMD5::HashAnsiString(*Settings.TargetNodeId);
		return FName(*FString::Printf(TEXT("__d2u_preset_%s_%s"), *Identity, Use));
	}

	FBindingPlan MakePlan(const FSettings& Settings)
	{
		FBindingPlan Plan;
		Plan.TargetWidgetName = Settings.TargetWidgetName;
		Plan.HoverPlaybackSpeed = Settings.HoverPlaybackSpeed;
		Plan.PressPlaybackSpeed = Settings.PressPlaybackSpeed;
		Plan.bOpacity = Settings.Preset == EPreset::ScreenFade || Settings.Preset == EPreset::ButtonFade;
		Plan.bTransform = !Plan.bOpacity;
		if (IsScreenPreset(Settings.Preset)) Plan.EntranceAnimationName = AnimationName(Settings, TEXT("entrance"));
		else
		{
			Plan.FeedbackAnimationName = AnimationName(Settings, TEXT("feedback"));
			Plan.HoverTime = Settings.DurationSeconds;
			Plan.PressTime = Settings.DurationSeconds * 2;
		}
		return Plan;
	}

	bool ReadTargetIds(UWidgetBlueprint* Blueprint, TMap<FName, FString>& TargetIds, FString& Error)
	{
		const FString Json = Design2UMGWidgetMetadata::PackageMetadata(Blueprint).GetValue(Blueprint, TargetIdsKey);
		if (Json.IsEmpty()) return true;
		TSharedPtr<FJsonObject> Object;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object) || !Object)
		{
			Error = TEXT("Stored source animation target identities are invalid.");
			return false;
		}
		for (const auto& Pair : Object->Values)
		{
			FString Id;
			if (!Pair.Value->TryGetString(Id)) { Error = TEXT("Stored source animation target identity is invalid."); return false; }
			TargetIds.Add(FName(*Pair.Key), Id);
		}
		return true;
	}

	bool ResolveStoredTarget(FSettings& Settings, const TMap<FName, FString>& TargetIds, FString& Error)
	{
		if (Settings.TargetNodeId.IsEmpty()) return true;
		FName Match;
		int32 Matches = 0;
		for (const auto& Pair : TargetIds)
			if (Pair.Value == Settings.TargetNodeId) { Match = Pair.Key; ++Matches; }
		if (Matches != 1)
		{
			Error = FString::Printf(TEXT("Animation preset source target '%s' does not resolve to one current widget; its settings were preserved."), *Settings.TargetNodeId);
			return false;
		}
		Settings.TargetWidgetName = Match;
		return true;
	}

	bool Validate(const FSettings& Settings, FString& Error)
	{
		if (static_cast<uint8>(Settings.Preset) >= UE_ARRAY_COUNT(PresetNames)
			|| !FMath::IsFinite(Settings.DurationSeconds) || Settings.DurationSeconds <= 0
			|| !FMath::IsFinite(Settings.Distance) || !FMath::IsFinite(Settings.PressDistance)
			|| !FMath::IsFinite(Settings.Scale) || Settings.Scale <= 0
			|| !FMath::IsFinite(Settings.PressScale) || Settings.PressScale <= 0
			|| !FMath::IsFinite(Settings.Opacity) || Settings.Opacity < 0 || Settings.Opacity > 1
			|| !FMath::IsFinite(Settings.PressOpacity) || Settings.PressOpacity < 0 || Settings.PressOpacity > 1
			|| !FMath::IsFinite(Settings.Overshoot) || Settings.Overshoot < 0
			|| !FMath::IsFinite(Settings.HoverPlaybackSpeed) || Settings.HoverPlaybackSpeed <= 0
			|| !FMath::IsFinite(Settings.PressPlaybackSpeed) || Settings.PressPlaybackSpeed <= 0)
		{
			Error = TEXT("Animation preset parameters require finite positive timing and scale, and opacity between zero and one.");
			return false;
		}
		return true;
	}

	bool ReadSettings(UWidgetBlueprint* Blueprint, TArray<FSettings>& Settings, FString& Error)
	{
		const FString Json = Design2UMGWidgetMetadata::PackageMetadata(Blueprint).GetValue(Blueprint, MetadataKey);
		if (Json.IsEmpty()) return true;
		TArray<TSharedPtr<FJsonValue>> Records;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Records))
		{
			Error = TEXT("Stored animation preset settings are invalid; they were preserved without applying.");
			return false;
		}
		for (const auto& Value : Records)
		{
			const auto Object = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			FString Target, Preset;
			FSettings Item;
			if (!Object || !Object->TryGetStringField(TEXT("target"), Target)
				|| !Object->TryGetStringField(TEXT("targetId"), Item.TargetNodeId)
				|| !Object->TryGetStringField(TEXT("preset"), Preset))
			{
				Error = TEXT("Stored animation preset target or type is missing.");
				return false;
			}
			int32 Type = INDEX_NONE;
			for (int32 Index = 0; Index < UE_ARRAY_COUNT(PresetNames); ++Index)
				if (Preset == PresetNames[Index]) Type = Index;
			if (Type == INDEX_NONE) { Error = TEXT("Stored animation preset type is unknown."); return false; }
			Item.Preset = static_cast<EPreset>(Type);
			Item.TargetWidgetName = FName(*Target);
			const struct { const TCHAR* Name; float* Field; } Fields[] = {
				{TEXT("duration"), &Item.DurationSeconds}, {TEXT("distance"), &Item.Distance},
				{TEXT("scale"), &Item.Scale}, {TEXT("opacity"), &Item.Opacity},
				{TEXT("pressScale"), &Item.PressScale}, {TEXT("pressDistance"), &Item.PressDistance},
				{TEXT("pressOpacity"), &Item.PressOpacity}, {TEXT("overshoot"), &Item.Overshoot},
				{TEXT("hoverSpeed"), &Item.HoverPlaybackSpeed}, {TEXT("pressSpeed"), &Item.PressPlaybackSpeed}};
			for (const auto& Field : Fields)
			{
				double Number;
				if (!Object->TryGetNumberField(Field.Name, Number)) { Error = TEXT("Stored animation preset parameter is missing."); return false; }
				*Field.Field = Number;
			}
			if (!Validate(Item, Error)) return false;
			Settings.Add(Item);
		}
		return true;
	}

	void WriteSettings(UWidgetBlueprint* Blueprint, const TArray<FSettings>& Settings)
	{
		TArray<TSharedPtr<FJsonValue>> Records;
		for (const auto& Item : Settings)
		{
			auto Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("target"), Item.TargetWidgetName.ToString());
			Object->SetStringField(TEXT("targetId"), Item.TargetNodeId);
			Object->SetStringField(TEXT("preset"), PresetNames[static_cast<uint8>(Item.Preset)]);
			const struct { const TCHAR* Name; float Value; } Fields[] = {
				{TEXT("duration"), Item.DurationSeconds}, {TEXT("distance"), Item.Distance},
				{TEXT("scale"), Item.Scale}, {TEXT("opacity"), Item.Opacity},
				{TEXT("pressScale"), Item.PressScale}, {TEXT("pressDistance"), Item.PressDistance},
				{TEXT("pressOpacity"), Item.PressOpacity}, {TEXT("overshoot"), Item.Overshoot},
				{TEXT("hoverSpeed"), Item.HoverPlaybackSpeed}, {TEXT("pressSpeed"), Item.PressPlaybackSpeed}};
			for (const auto& Field : Fields) Object->SetNumberField(Field.Name, Field.Value);
			Records.Add(MakeShared<FJsonValueObject>(Object));
		}
		FString Json;
		FJsonSerializer::Serialize(Records, TJsonWriterFactory<>::Create(&Json));
		SetMetadata(Blueprint, MetadataKey, Json);
	}

	void ReplaceOwnedAnimation(UWidgetBlueprint* Blueprint, FName Name)
	{
		for (int32 Index = Blueprint->Animations.Num() - 1; Index >= 0; --Index)
			if (Blueprint->Animations[Index]->GetFName() == Name)
				UMGPrototypeAnimationBuilder::RemoveAnimation(Blueprint, Blueprint->Animations[Index]);
	}

	void BuildFeedback(UWidgetBlueprint* Blueprint, UWidget* Widget, const FSettings& Settings, FName Name)
	{
		ReplaceOwnedAnimation(Blueprint, Name);
		auto* Animation = UMGPrototypeAnimationBuilder::CreateAnimation(Blueprint, Name, Settings.DurationSeconds * 2);
		SetMetadata(Animation, AnimationOwnerKey, TEXT("1"));
		const FGuid Binding = UMGPrototypeAnimationBuilder::BindWidget(Animation, Widget);
		const FWidgetTransform Idle = Widget->GetRenderTransform();
		FWidgetTransform Hover = Idle;
		FWidgetTransform Press = Idle;
		const float IdleOpacity = Widget->GetRenderOpacity();
		float HoverOpacity = IdleOpacity;
		float PressOpacity = IdleOpacity;
		switch (Settings.Preset)
		{
		case EPreset::ButtonScale: case EPreset::ButtonPop:
			Hover.Scale *= Settings.Scale; Press.Scale *= Settings.PressScale; break;
		case EPreset::ButtonLift:
			Hover.Translation.Y -= Settings.Distance; Press.Translation.Y -= Settings.PressDistance; break;
		case EPreset::ButtonFade:
			HoverOpacity *= Settings.Opacity; PressOpacity *= Settings.PressOpacity; break;
		case EPreset::ButtonScaleAndLift:
			Hover.Scale *= Settings.Scale; Press.Scale *= Settings.PressScale;
			Hover.Translation.Y -= Settings.Distance; Press.Translation.Y -= Settings.PressDistance; break;
		default: break;
		}
		const auto WritePoseChannel = [&](FMovieSceneFloatChannel& Channel, float From, float Middle, float To)
		{
			Channel.SetTickResolution(Animation->MovieScene->GetTickResolution());
			const auto Frame = [&](double Alpha) { return FFrameNumber(static_cast<int32>(FMath::RoundToInt64(
				Animation->MovieScene->GetTickResolution().AsDecimal() * Settings.DurationSeconds * Alpha))); };
			Channel.AddCubicKey(0, From, RCTM_Auto);
			if (Settings.Preset == EPreset::ButtonPop)
				Channel.AddCubicKey(Frame(0.65), Middle + (Middle - From) * Settings.Overshoot, RCTM_Auto);
			Channel.AddCubicKey(Frame(1), Middle, RCTM_Auto);
			if (Settings.Preset == EPreset::ButtonPop)
				Channel.AddCubicKey(Frame(1.65), To + (To - Middle) * Settings.Overshoot, RCTM_Auto);
			Channel.AddCubicKey(Frame(2), To, RCTM_Auto);
		};
		if (Idle != Hover || Idle != Press)
		{
			const float HoverValues[] = {static_cast<float>(Hover.Translation.X), static_cast<float>(Hover.Translation.Y),
				static_cast<float>(Hover.Scale.X), static_cast<float>(Hover.Scale.Y),
				static_cast<float>(Hover.Shear.X), static_cast<float>(Hover.Shear.Y), Hover.Angle};
			int32 ChannelIndex = 0;
			const auto Keys = [&](FMovieSceneFloatChannel& Channel, float From, float To)
				{ WritePoseChannel(Channel, From, HoverValues[ChannelIndex++], To); };
			UMGPrototypeAnimationBuilder::AddTransformTrack(Animation, Binding, Idle, Press, Keys);
		}
		if (!FMath::IsNearlyEqual(IdleOpacity, HoverOpacity) || !FMath::IsNearlyEqual(IdleOpacity, PressOpacity))
		{
			const auto Keys = [&](FMovieSceneFloatChannel& Channel, float From, float To)
				{ WritePoseChannel(Channel, From, HoverOpacity, To); };
			UMGPrototypeAnimationBuilder::AddOpacityTrack(Animation, Binding, IdleOpacity, PressOpacity, Keys);
		}
	}

}

bool IsScreenPreset(EPreset Preset) { return Preset <= EPreset::ScreenScalePop; }

bool IsOwnedAnimation(const UWidgetAnimation* Animation)
{
	return Animation && Design2UMGWidgetMetadata::PackageMetadata(const_cast<UWidgetAnimation*>(Animation))
		.GetValue(Animation, AnimationOwnerKey) == TEXT("1");
}

FSettings::FSettings(EPreset InPreset) : Preset(InPreset)
{
	const bool bScreen = IsScreenPreset(InPreset);
	DurationSeconds = bScreen ? 0.25f : 0.12f;
	Distance = bScreen ? 32.0f : 4.0f;
	Scale = bScreen ? 0.9f : 1.05f;
	Opacity = bScreen ? 0.0f : 0.7f;
}

FText GetPresetLabel(EPreset Preset)
{
	switch (Preset)
	{
	case EPreset::ScreenFade: return LOCTEXT("ScreenFade", "Screen: Fade in");
	case EPreset::ScreenSlideLeft: return LOCTEXT("ScreenSlideLeft", "Screen: Slide from left");
	case EPreset::ScreenSlideRight: return LOCTEXT("ScreenSlideRight", "Screen: Slide from right");
	case EPreset::ScreenSlideTop: return LOCTEXT("ScreenSlideTop", "Screen: Slide from top");
	case EPreset::ScreenSlideBottom: return LOCTEXT("ScreenSlideBottom", "Screen: Slide from bottom");
	case EPreset::ScreenScalePop: return LOCTEXT("ScreenScalePop", "Screen: Scale pop");
	case EPreset::ButtonScale: return LOCTEXT("ButtonScale", "Button: Scale");
	case EPreset::ButtonLift: return LOCTEXT("ButtonLift", "Button: Lift");
	case EPreset::ButtonFade: return LOCTEXT("ButtonFade", "Button: Fade");
	case EPreset::ButtonScaleAndLift: return LOCTEXT("ButtonScaleAndLift", "Button: Scale and lift");
	case EPreset::ButtonPop: return LOCTEXT("ButtonPop", "Button: Pop");
	}
	return FText::GetEmpty();
}

bool BuildAnimations(UWidgetBlueprint* Blueprint, const FSettings& Settings,
	FBindingPlan& OutPlan, FString& OutError)
{
	if (!Validate(Settings, OutError)) return false;
	UWidget* Widget = Blueprint && Blueprint->WidgetTree ? Blueprint->WidgetTree->FindWidget(Settings.TargetWidgetName) : nullptr;
	if (!Widget || (!IsScreenPreset(Settings.Preset) && !Cast<UButton>(Widget)))
	{
		OutError = TEXT("Select an existing widget for screen entrance, or a native Button for feedback.");
		return false;
	}
	OutPlan = MakePlan(Settings);
	const FName OwnedName = IsScreenPreset(Settings.Preset) ? OutPlan.EntranceAnimationName : OutPlan.FeedbackAnimationName;
	for (const UWidgetAnimation* Existing : Blueprint->Animations)
		if (Existing && Existing->GetFName() == OwnedName && !IsOwnedAnimation(Existing))
		{
			OutError = TEXT("The preset animation name is already used by a user animation; it was preserved.");
			return false;
		}
	if (!IsScreenPreset(Settings.Preset))
	{
		BuildFeedback(Blueprint, Widget, Settings, OutPlan.FeedbackAnimationName);
		return true;
	}
	ReplaceOwnedAnimation(Blueprint, OutPlan.EntranceAnimationName);
	auto* Animation = UMGPrototypeAnimationBuilder::CreateAnimation(Blueprint, OutPlan.EntranceAnimationName, Settings.DurationSeconds);
	SetMetadata(Animation, AnimationOwnerKey, TEXT("1"));
	const FGuid Binding = UMGPrototypeAnimationBuilder::BindWidget(Animation, Widget);
	const FWidgetTransform End = Widget->GetRenderTransform();
	FWidgetTransform Start = End;
	float StartOpacity = Widget->GetRenderOpacity();
	switch (Settings.Preset)
	{
	case EPreset::ScreenFade: StartOpacity *= Settings.Opacity; break;
	case EPreset::ScreenSlideLeft: Start.Translation.X -= Settings.Distance; break;
	case EPreset::ScreenSlideRight: Start.Translation.X += Settings.Distance; break;
	case EPreset::ScreenSlideTop: Start.Translation.Y -= Settings.Distance; break;
	case EPreset::ScreenSlideBottom: Start.Translation.Y += Settings.Distance; break;
	case EPreset::ScreenScalePop: Start.Scale *= Settings.Scale; break;
	default: break;
	}
	const auto Keys = [&](FMovieSceneFloatChannel& Channel, float From, float To)
	{
		Channel.SetTickResolution(Animation->MovieScene->GetTickResolution());
		const auto Frame = [&](double Alpha) { return FFrameNumber(static_cast<int32>(FMath::RoundToInt64(
			Animation->MovieScene->GetTickResolution().AsDecimal() * Settings.DurationSeconds * Alpha))); };
		Channel.AddCubicKey(0, From, RCTM_Auto);
		if (Settings.Preset == EPreset::ScreenScalePop)
			Channel.AddCubicKey(Frame(0.7), To + (To - From) * Settings.Overshoot, RCTM_Auto);
		Channel.AddCubicKey(Frame(1), To, RCTM_Auto);
	};
	if (Start != End) UMGPrototypeAnimationBuilder::AddTransformTrack(Animation, Binding, Start, End, Keys);
	if (!FMath::IsNearlyEqual(StartOpacity, Widget->GetRenderOpacity()))
		UMGPrototypeAnimationBuilder::AddOpacityTrack(Animation, Binding, StartOpacity, Widget->GetRenderOpacity(), Keys);
	return true;
}

bool Apply(UWidgetBlueprint* Blueprint, const FSettings& InputSettings, TArray<FString>& OutWarnings)
{
	FSettings Settings = InputSettings;
	FString Error;
	if (!Blueprint || !Validate(Settings, Error)) { OutWarnings.Add(Error); return false; }
	TMap<FName, FString> TargetIds;
	if (!ReadTargetIds(Blueprint, TargetIds, Error)) { OutWarnings.Add(Error); return false; }
	if (const FString* Id = TargetIds.Find(Settings.TargetWidgetName)) Settings.TargetNodeId = *Id;
	TArray<FSettings> Stored;
	if (!ReadSettings(Blueprint, Stored, Error)) { OutWarnings.Add(Error); return false; }
	const FBindingPlan Plan = MakePlan(Settings);
	if (!Design2UMGPrototypeGraphWiring::ValidateAnimationPresetBindings(Blueprint, Plan, Error))
	{
		OutWarnings.Add(Error);
		return false;
	}
	if (!GEditor || !GEditor->CanTransact() || GEditor->IsTransactionActive())
	{
		OutWarnings.Add(TEXT("Apply animation presets after the current editor transaction has finished, with editor undo available."));
		return false;
	}
	TArray<UObject*> OriginalObjects;
	GetObjectsWithOuter(Blueprint, OriginalObjects, true);
	const TSet<UObject*> ExistingObjects(OriginalObjects);
	const bool bWasDirty = Blueprint->GetOutermost()->IsDirty();
	const EBlueprintStatus OriginalStatus = Blueprint->Status;
	FGuid TransactionId;
	bool bApplied = false;
	{
		const FScopedTransaction Transaction(LOCTEXT("ApplyTransaction", "Apply UI animation preset"));
		TransactionId = GUndo ? GUndo->GetContext().TransactionId : FGuid();
		Blueprint->SetFlags(RF_Transactional);
		Blueprint->Modify();
		for (UObject* Object : OriginalObjects)
		{
			Object->SetFlags(RF_Transactional);
			Object->Modify();
		}
		FBindingPlan Built;
		if (BuildAnimations(Blueprint, Settings, Built, Error))
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			if (Design2UMGPrototypeGraphWiring::WireAnimationPresetBindings(Blueprint, Built, OutWarnings))
			{
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				bApplied = Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings;
				if (!bApplied) OutWarnings.Add(TEXT("Animation preset compilation failed."));
				else
				{
					Stored.RemoveAll([&](const FSettings& Item) { return (Settings.TargetNodeId.IsEmpty()
						? Item.TargetWidgetName == Settings.TargetWidgetName : Item.TargetNodeId == Settings.TargetNodeId)
						&& IsScreenPreset(Item.Preset) == IsScreenPreset(Settings.Preset); });
					Stored.Add(Settings);
					WriteSettings(Blueprint, Stored);
					Blueprint->MarkPackageDirty();
				}
			}
		}
		else OutWarnings.Add(Error);
	}
	if (!bApplied)
	{
		// Free replacement names before undo restores the original animation subobjects.
		TArray<UObject*> CurrentObjects;
		GetObjectsWithOuter(Blueprint, CurrentObjects, true);
		for (UObject* Object : CurrentObjects)
			if (!ExistingObjects.Contains(Object) && Object->IsA<UWidgetAnimation>())
			{
				Design2UMGWidgetMetadata::PackageMetadata(Object).RemoveValue(Object, AnimationOwnerKey);
				Object->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
		bool bRestored = GeneratedAssetLifecycle::UndoOwnedTransaction(TransactionId);
		if (bRestored)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			bRestored = Blueprint->Status == BS_UpToDate || Blueprint->Status == BS_UpToDateWithWarnings
				|| (OriginalStatus == BS_Error && Blueprint->Status == OriginalStatus);
			if (bRestored) Blueprint->GetOutermost()->SetDirtyFlag(bWasDirty);
		}
		OutWarnings.Add(bRestored ? TEXT("The failed preset operation was automatically undone; the original asset was restored.")
			: TEXT("The failed preset operation could not be fully restored; do not save this asset."));
		return false;
	}
	return true;
}

void Restore(UWidgetBlueprint* Blueprint, TArray<FString>& OutWarnings)
{
	TArray<FSettings> Stored;
	FString Error;
	if (!ReadSettings(Blueprint, Stored, Error)) { OutWarnings.Add(Error); return; }
	TMap<FName, FString> TargetIds;
	if (!ReadTargetIds(Blueprint, TargetIds, Error)) { OutWarnings.Add(Error); return; }
	for (FSettings& Settings : Stored)
	{
		const FName PreviousTarget = Settings.TargetWidgetName;
		if (!ResolveStoredTarget(Settings, TargetIds, Error)) { OutWarnings.Add(Error); continue; }
		FBindingPlan Plan = MakePlan(Settings);
		if (!Design2UMGPrototypeGraphWiring::ValidateAnimationPresetBindings(Blueprint, Plan, Error))
		{
			OutWarnings.Add(Error);
			continue;
		}
		const FName Name = IsScreenPreset(Settings.Preset) ? Plan.EntranceAnimationName : Plan.FeedbackAnimationName;
		const auto* Found = Blueprint->Animations.FindByPredicate([&](const UWidgetAnimation* Animation)
			{ return Animation && Animation->GetFName() == Name; });
		if (Found)
		{
			UWidgetAnimation* Animation = Found->Get();
			if (!IsOwnedAnimation(Animation))
			{
				OutWarnings.Add(TEXT("The stored preset animation name is used by a user animation; it was preserved."));
				continue;
			}
			for (FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
			{
				if (Binding.WidgetName != PreviousTarget) continue;
				Animation->Modify();
				Animation->MovieScene->Modify();
				Binding.WidgetName = Settings.TargetWidgetName;
				if (FMovieScenePossessable* Possessable = Animation->MovieScene->FindPossessable(Binding.AnimationGuid))
					Possessable->SetName(Settings.TargetWidgetName.ToString());
			}
		}
		if (!Found && !BuildAnimations(Blueprint, Settings, Plan, Error))
		{
			OutWarnings.Add(Error);
			continue;
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		Design2UMGPrototypeGraphWiring::WireAnimationPresetBindings(Blueprint, Plan, OutWarnings);
	}
	if (!Stored.IsEmpty()) WriteSettings(Blueprint, Stored);
}

void RecordSourceTargets(UWidgetBlueprint* Blueprint, const TMap<FName, FString>& TargetIds)
{
	auto Object = MakeShared<FJsonObject>();
	for (const auto& Pair : TargetIds) Object->SetStringField(Pair.Key.ToString(), Pair.Value);
	FString Json;
	FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Json));
	SetMetadata(Blueprint, TargetIdsKey, Json);
}

}

#undef LOCTEXT_NAMESPACE
