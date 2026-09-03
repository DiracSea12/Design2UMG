// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PencilExtensionResources.h"
#include "PencilExtensionLocator.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "FontResolver.h"

#include "Dom/JsonObject.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "PencilExtensionResources"

namespace
{
	/** Metadata key set on documents whose icon/library references could not be backed by extension resources. */
	constexpr const TCHAR* PenExtensionResourceGapMetadataKey = TEXT("design2umg.penExtensionResourceGap");

	struct FPencilResourceState
	{
		/** True after the extension icon index was located and parsed in this process. */
		bool bIconIndexLoaded = false;

		/** Detailed "resource not found" error log fires once per process; per-import warnings keep firing. */
		bool bLoggedMissingIconIndex = false;

		/** The editor notification pops at most once per editor session. */
		bool bNotifiedMissingResources = false;

		FString IconIndexPath;
		FString ExtensionVersion;
		TArray<FString> LastTriedIconIndexPaths;

		/** Parsed tables kept in memory so registration can be restored after FFontResolver::ClearCache()
		 * drains the resolver caches (it runs whenever a project font asset changes during imports). */
		TMap<FString, TMap<FString, FString>> GlyphsByFamily;
		TMap<FString, TMap<FString, FString>> NamesByFamily;
	};

	FPencilResourceState& GetResourceState()
	{
		static FPencilResourceState State;
		return State;
	}

	/** Convert a Unicode codepoint (may exceed 0xFFFF) into its UTF-16 FString glyph. */
	bool TryCodepointToGlyphString(const int64 Codepoint, FString& OutGlyph)
	{
		if (Codepoint < 1 || Codepoint > 0x10FFFF || (Codepoint >= 0xD800 && Codepoint <= 0xDFFF))
		{
			return false;
		}

		OutGlyph.Reset();
		if (Codepoint <= 0xFFFF)
		{
			OutGlyph.AppendChar(static_cast<TCHAR>(Codepoint));
		}
		else
		{
			const uint32 Offset = static_cast<uint32>(Codepoint) - 0x10000;
			OutGlyph.AppendChar(static_cast<TCHAR>(0xD800 + (Offset >> 10)));
			OutGlyph.AppendChar(static_cast<TCHAR>(0xDC00 + (Offset & 0x3FF)));
		}
		return true;
	}

	bool ContainsIconNode(const TArray<TSharedPtr<FDesignNode>>& Nodes)
	{
		for (const TSharedPtr<FDesignNode>& Node : Nodes)
		{
			if (!Node.IsValid())
			{
				continue;
			}
			if (Node->Type == EDesignNodeType::IconFont)
			{
				return true;
			}
			if (ContainsIconNode(Node->Children))
			{
				return true;
			}
		}
		return false;
	}

	bool DocumentUsesExtensionResources(const FDesignDocument& Document, bool& bOutUsesIcons, bool& bOutUsesLibraryVariables)
	{
		bOutUsesIcons = ContainsIconNode(Document.Children);
		bOutUsesLibraryVariables = false;
		for (const TPair<FString, FDesignVariable>& Pair : Document.Variables)
		{
			if (!Pair.Value.SourceAlias.IsEmpty())
			{
				bOutUsesLibraryVariables = true;
				break;
			}
		}
		return bOutUsesIcons || bOutUsesLibraryVariables;
	}

	void ShowMissingResourcesNotification()
	{
		FNotificationInfo Info(LOCTEXT(
			"PencilExtensionResourcesMissing",
			"Pencil extension resources not found (icon codepoint table / library variables). Icons in this design will fall back to icon-name text, and library variables may not resolve. Install the Pencil VS Code extension, or set the Pencil extension directory in the Design2UMG settings."));
		Info.ExpireDuration = 12.0f;
		Info.bFireAndForget = false;
		Info.bUseSuccessFailIcons = true;
		Info.CheckBoxText = LOCTEXT("PencilExtensionResourcesMissingDontShowAgain", "Don't show again");
		Info.CheckBoxStateChanged = FOnCheckStateChanged::CreateLambda([](ECheckBoxState NewState)
		{
			if (NewState == ECheckBoxState::Checked)
			{
				if (UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get())
				{
					Settings->bSuppressPencilExtensionResourceWarning = true;
					Settings->SaveConfig();
				}
			}
		});

		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
}

namespace PencilExtensionResources
{
	void GetIconIndexRelativeCandidates(TArray<FString>& OutRelativePaths)
	{
		OutRelativePaths = {
			TEXT("node_modules/@ha/pencil-editor/src/managers/data/icon-index.json"),
		};
	}

	bool ParseIconIndexJson(
		const FString& Json,
		TMap<FString, TMap<FString, FString>>& OutGlyphsByFamily,
		TMap<FString, TMap<FString, FString>>& OutNamesByFamily,
		FString& OutError)
	{
		OutGlyphsByFamily.Reset();
		OutNamesByFamily.Reset();
		OutError.Reset();

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = TEXT("icon-index payload is not valid JSON");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Sets = nullptr;
		if (!Root->TryGetArrayField(TEXT("sets"), Sets) || Sets->Num() == 0)
		{
			OutError = TEXT("icon-index payload has no icon sets");
			return false;
		}

		int32 TotalIcons = 0;
		for (const TSharedPtr<FJsonValue>& SetValue : *Sets)
		{
			const TSharedPtr<FJsonObject>& SetObject = SetValue->AsObject();
			if (!SetObject.IsValid())
			{
				OutError = TEXT("icon-index set entry is not an object");
				return false;
			}

			FString SetName;
			SetObject->TryGetStringField(TEXT("name"), SetName);

			const TArray<TSharedPtr<FJsonValue>>* Families = nullptr;
			if (!SetObject->TryGetArrayField(TEXT("families"), Families) || Families->Num() == 0)
			{
				OutError = FString::Printf(TEXT("icon set '%s' declares no families"), *SetName);
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* Icons = nullptr;
			if (!SetObject->TryGetArrayField(TEXT("icons"), Icons))
			{
				OutError = FString::Printf(TEXT("icon set '%s' declares no icons"), *SetName);
				return false;
			}

			for (const TSharedPtr<FJsonValue>& IconValue : *Icons)
			{
				const TSharedPtr<FJsonObject>& IconObject = IconValue->AsObject();
				if (!IconObject.IsValid())
				{
					OutError = FString::Printf(TEXT("icon set '%s' has a non-object icon entry"), *SetName);
					return false;
				}

				FString IconName;
				int64 Codepoint = 0;
				if (!IconObject->TryGetStringField(TEXT("name"), IconName) || IconName.IsEmpty())
				{
					OutError = FString::Printf(TEXT("icon set '%s' has an icon entry without a name"), *SetName);
					return false;
				}
				if (!IconObject->TryGetNumberField(TEXT("codepoint"), Codepoint))
				{
					OutError = FString::Printf(TEXT("icon '%s' in set '%s' has no codepoint"), *IconName, *SetName);
					return false;
				}

				FString Glyph;
				if (!TryCodepointToGlyphString(Codepoint, Glyph))
				{
					OutError = FString::Printf(TEXT("icon '%s' in set '%s' has an invalid codepoint %lld"), *IconName, *SetName, Codepoint);
					return false;
				}

				const FString IconKey = FFontResolver::NormalizeIconNameKey(IconName);
				if (IconKey.IsEmpty())
				{
					continue;
				}

				for (const TSharedPtr<FJsonValue>& FamilyValue : *Families)
				{
					const FString FamilyName = FamilyValue->AsString();
					if (FamilyName.IsEmpty())
					{
						continue;
					}
					OutGlyphsByFamily.FindOrAdd(FamilyName).Add(IconKey, Glyph);
					OutNamesByFamily.FindOrAdd(FamilyName).Add(Glyph, IconName);
				}
				++TotalIcons;
			}
		}

		if (OutGlyphsByFamily.Num() == 0)
		{
			OutError = TEXT("icon-index payload produced no family tables");
			return false;
		}

		UE_LOG(LogDesign2UMG, Verbose, TEXT("PencilExtensionResources: parsed icon index: %d icons across %d families"), TotalIcons, OutGlyphsByFamily.Num());
		return true;
	}

	bool IsIconIndexRegistered()
	{
		return GetResourceState().bIconIndexLoaded;
	}

	void EnsureIconGlyphsRegistered()
	{
		FPencilResourceState& State = GetResourceState();
		if (!State.bIconIndexLoaded)
		{
			TArray<PencilExtensionLocator::FExtensionRoot> Candidates;
			TArray<FString> SearchedDirs;
			PencilExtensionLocator::GetExtensionRootCandidates(Candidates, SearchedDirs);

			TArray<FString> RelativeCandidates;
			GetIconIndexRelativeCandidates(RelativeCandidates);

			// Tried paths are recorded for the not-found diagnostics; the first existing candidate wins.
			TArray<FString> CandidatePaths;
			TArray<FString> CandidateVersions;
			for (const PencilExtensionLocator::FExtensionRoot& Candidate : Candidates)
			{
				for (const FString& RelativePath : RelativeCandidates)
				{
					CandidatePaths.Add(Candidate.Directory / RelativePath);
					CandidateVersions.Add(Candidate.Version);
				}
			}
			State.LastTriedIconIndexPaths = CandidatePaths;

			FString LoadedJson;
			FString LoadedPath;
			FString LoadedVersion;
			for (int32 Index = 0; Index < CandidatePaths.Num(); ++Index)
			{
				if (FFileHelper::LoadFileToString(LoadedJson, *CandidatePaths[Index]))
				{
					LoadedPath = CandidatePaths[Index];
					LoadedVersion = CandidateVersions[Index];
					break;
				}
			}

			if (LoadedPath.IsEmpty())
			{
				if (!State.bLoggedMissingIconIndex)
				{
					State.bLoggedMissingIconIndex = true;
					const FString TriedList = FString::Join(State.LastTriedIconIndexPaths, TEXT("\n  "));
					UE_LOG(LogDesign2UMG, Error,
						TEXT("PencilExtensionResources: icon codepoint index (icon-index.json) not found; icon widgets will fall back to icon-name text.\nInstall the Pencil VS Code extension or set the extension directory in the Design2UMG settings. Tried candidates:\n  %s"),
						*TriedList);
				}
				return;
			}

			FString ParseError;
			if (!ParseIconIndexJson(LoadedJson, State.GlyphsByFamily, State.NamesByFamily, ParseError))
			{
				State.GlyphsByFamily.Reset();
				State.NamesByFamily.Reset();
				if (!State.bLoggedMissingIconIndex)
				{
					State.bLoggedMissingIconIndex = true;
					UE_LOG(LogDesign2UMG, Error,
						TEXT("PencilExtensionResources: failed to parse icon index '%s': %s"),
						*LoadedPath, *ParseError);
				}
				return;
			}

			State.bIconIndexLoaded = true;
			State.IconIndexPath = MoveTemp(LoadedPath);
			State.ExtensionVersion = MoveTemp(LoadedVersion);
			UE_LOG(LogDesign2UMG, Display,
				TEXT("PencilExtensionResources: loaded icon index: %d families from '%s' (extension version %s)"),
				State.GlyphsByFamily.Num(), *State.IconIndexPath, *State.ExtensionVersion);
		}

		// FFontResolver::ClearCache() drains the icon tables whenever a project font asset
		// changes (for example during font auto-import); re-register from the parsed tables.
		if (FFontResolver::HasRegisteredIconGlyphs())
		{
			return;
		}
		for (const TPair<FString, TMap<FString, FString>>& Pair : State.GlyphsByFamily)
		{
			const TMap<FString, FString>* Names = State.NamesByFamily.Find(Pair.Key);
			FFontResolver::RegisterKnownIconGlyphs(Pair.Key, Pair.Value, Names ? *Names : TMap<FString, FString>());
		}
		UE_LOG(LogDesign2UMG, Verbose,
			TEXT("PencilExtensionResources: (re)registered icon glyphs for %d families"),
			State.GlyphsByFamily.Num());
	}

	void ReportPenResourceGaps(FDesignDocument& Document)
	{
		FPencilResourceState& State = GetResourceState();
		if (State.bIconIndexLoaded)
		{
			return;
		}

		bool bUsesIcons = false;
		bool bUsesLibraryVariables = false;
		if (!DocumentUsesExtensionResources(Document, bUsesIcons, bUsesLibraryVariables))
		{
			return;
		}

		TArray<FString> UsedKinds;
		if (bUsesIcons)
		{
			UsedKinds.Add(TEXT("icons"));
		}
		if (bUsesLibraryVariables)
		{
			UsedKinds.Add(TEXT("library variables"));
		}
		UE_LOG(LogDesign2UMG, Warning,
			TEXT("PencilExtensionResources: pen document uses %s but no Pencil extension icon index is registered; icons will render as icon-name text. Install the Pencil VS Code extension or set the extension directory in the Design2UMG settings."),
			*FString::Join(UsedKinds, TEXT(" and ")));
		Document.Metadata.Add(PenExtensionResourceGapMetadataKey, TEXT("icon-index-missing"));

		const UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
		if (State.bNotifiedMissingResources || (Settings && Settings->bSuppressPencilExtensionResourceWarning))
		{
			return;
		}
		State.bNotifiedMissingResources = true;
		ShowMissingResourcesNotification();
	}
}

#undef LOCTEXT_NAMESPACE
