// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PencilExtensionResources.h"
#include "PencilExtensionLocator.h"
#include "Design2UMGLog.h"
#include "Design2UMGSettings.h"
#include "DesignJsonUtils.h"
#include "DesignSchema.h"
#include "FontResolver.h"
#include "PenBundledLibraries.h"
#include "PenParser.h"

#include "Dom/JsonObject.h"
#include "Framework/Notifications/NotificationManager.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

		/** True after at least one bundled library variables table was registered in this process. */
		bool bLibrariesLoaded = false;

		/** Detailed "resource not found" error logs fire once per process; per-import warnings keep firing. */
		bool bLoggedMissingIconIndex = false;
		bool bLoggedMissingLibraries = false;

		/** The editor notification pops at most once per editor session. */
		bool bNotifiedMissingResources = false;

		FString IconIndexPath;
		FString ExtensionVersion;
		TArray<FString> LastTriedIconIndexPaths;
		TArray<FString> LastTriedLibraryDirs;

		/** Parsed tables kept in memory so registration can be restored after FFontResolver::ClearCache()
		 * drains the resolver caches (it runs whenever a project font asset changes during imports). */
		TMap<FString, TMap<FString, FString>> GlyphsByFamily;
		TMap<FString, TMap<FString, FString>> NamesByFamily;
		TArray<PencilExtensionResources::FFontStyle> FontStyles;
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
		// Pinned (bFireAndForget=false) but auto-expires so an unattended import
		// session is not left with a permanent notification.
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
			TEXT("out/editor/assets/index.js"),
		};
	}

	bool ExtractIconIndexJson(const FString& Bundle, FString& OutJson, FString& OutError)
	{
		OutJson.Reset();
		OutError.Reset();
		// The packaged registry is strict JSON in an unescaped single-quoted literal,
		// not an object expression. Select its schema without evaluating surrounding code.
		const FRegexPattern Pattern(TEXT("JSON\\.parse\\(\\s*'([^'\\\\]*)'\\s*\\)"));
		FRegexMatcher Matcher(Pattern, Bundle);
		while (Matcher.FindNext())
		{
			FString Candidate = TEXT("{\"sets\":") + Matcher.GetCaptureGroup(1) + TEXT("}");
			TSharedPtr<FJsonObject> Root;
			if (!DesignJsonUtils::TryParseObject(Candidate, Root))
				continue;
			const TArray<TSharedPtr<FJsonValue>>* Sets = nullptr;
			if (!Root->TryGetArrayField(TEXT("sets"), Sets) || Sets->IsEmpty())
				continue;
			const bool bHasNonIconSet = Sets->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Set) {
				return !Set.IsValid() || Set->Type != EJson::Object ||
				       !Set->AsObject()->HasTypedField<EJson::Array>(TEXT("families")) ||
				       !Set->AsObject()->HasTypedField<EJson::Array>(TEXT("icons"));
			});
			if (bHasNonIconSet)
				continue;
			OutJson = MoveTemp(Candidate);
			return true;
		}
		OutError = TEXT("Editor bundle contains no static JSON icon registry.");
		return false;
	}

	int32 FFontStyle::ResolveWeight(int32 RequestedWeight) const
	{
		return WeightRange.IsSet()
			? FMath::Clamp(RequestedWeight, FMath::CeilToInt32(WeightRange->X), FMath::FloorToInt32(WeightRange->Y))
			: Weight;
	}

	bool ExtractFontStyles(const FString& Bundle, const TSet<FString>& Families,
		TArray<FFontStyle>& OutStyles, FString& OutError)
	{
		OutStyles.Reset();
		OutError.Reset();
		const FRegexPattern StartPattern(TEXT(R"(\{name:("(?:[^"\\]|\\.)*"),styles:\[)"));
		const FRegexPattern KeyPattern(TEXT(R"("(?:[^"\\]|\\.)*"|\b(name|styles|url|weight|italic|axes|tag|start|end)\s*:|(!0|!1))"));
		FRegexMatcher Starts(StartPattern, Bundle);
		while (Starts.FindNext())
		{
			TSharedPtr<FJsonObject> Name;
			if (!DesignJsonUtils::TryParseObject(TEXT("{\"name\":") + Starts.GetCaptureGroup(1) + TEXT("}"), Name)
				|| Name->GetStringField(TEXT("name")).IsEmpty()
				|| (!Families.IsEmpty() && !Families.Contains(Name->GetStringField(TEXT("name")))))
				continue;

			int32 Depth = 0;
			int32 End = Starts.GetMatchBeginning();
			bool bString = false;
			bool bEscape = false;
			for (; End < Bundle.Len(); ++End)
			{
				const TCHAR C = Bundle[End];
				if (bString)
				{
					if (bEscape) bEscape = false;
					else if (C == TEXT('\\')) bEscape = true;
					else if (C == TEXT('"')) bString = false;
				}
				else if (C == TEXT('"')) bString = true;
				else if (C == TEXT('{')) ++Depth;
				else if (C == TEXT('}') && --Depth == 0) break;
			}
			const FString Literal = Bundle.Mid(Starts.GetMatchBeginning(), End - Starts.GetMatchBeginning() + 1);
			FRegexMatcher Keys(KeyPattern, Literal);
			FString Json;
			int32 Position = 0;
			while (Keys.FindNext())
			{
				Json += Literal.Mid(Position, Keys.GetMatchBeginning() - Position);
				const FString Key = Keys.GetCaptureGroup(1);
				const FString Boolean = Keys.GetCaptureGroup(2);
				Json += !Key.IsEmpty() ? TEXT("\"") + Key + TEXT("\":")
					: !Boolean.IsEmpty() ? (Boolean == TEXT("!0") ? TEXT("true") : TEXT("false"))
					: Keys.GetCaptureGroup(0);
				Position = Keys.GetMatchEnding();
			}
			Json += Literal.Mid(Position);
			TSharedPtr<FJsonObject> Font;
			if (!DesignJsonUtils::TryParseObject(Json, Font))
			{
				OutError = TEXT("Invalid Pencil font description: ") + Name->GetStringField(TEXT("name"));
				OutStyles.Reset();
				return false;
			}
			for (const auto& Value : Font->GetArrayField(TEXT("styles")))
			{
				const auto Style = Value->AsObject();
				FFontStyle Parsed;
				Parsed.Family = Font->GetStringField(TEXT("name"));
				Parsed.Url = Style->GetStringField(TEXT("url"));
				Parsed.Weight = Style->GetIntegerField(TEXT("weight"));
				Style->TryGetBoolField(TEXT("italic"), Parsed.bItalic);
				const TArray<TSharedPtr<FJsonValue>>* Axes = nullptr;
				Parsed.bVariable = Style->TryGetArrayField(TEXT("axes"), Axes) && !Axes->IsEmpty();
				if (Parsed.bVariable)
					for (const auto& AxisValue : *Axes)
					{
						const auto Axis = AxisValue->AsObject();
						if (Axis->GetStringField(TEXT("tag")) == TEXT("wght"))
							Parsed.WeightRange = FVector2D(Axis->GetNumberField(TEXT("start")), Axis->GetNumberField(TEXT("end")));
					}
				OutStyles.Add(MoveTemp(Parsed));
			}
		}
		return true;
	}

	bool FindFontStyle(const FString& Family, int32 Weight, bool bItalic, FFontStyle& OutStyle)
	{
		EnsureIconGlyphsRegistered();
		const FFontStyle* Best = nullptr;
		for (const FFontStyle& Style : GetResourceState().FontStyles)
		{
			if (Style.Family.Equals(Family, ESearchCase::IgnoreCase) && Style.bItalic == bItalic
				&& (!Best || FMath::Abs(Style.ResolveWeight(Weight) - Weight) < FMath::Abs(Best->ResolveWeight(Weight) - Weight)))
				Best = &Style;
		}
		if (!Best) return false;
		OutStyle = *Best;
		return true;
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
		if (!DesignJsonUtils::TryParseObject(Json, Root))
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

			FString LoadedBundle;
			FString LoadedPath;
			FString LoadedVersion;
			for (int32 Index = 0; Index < CandidatePaths.Num(); ++Index)
			{
				if (FFileHelper::LoadFileToString(LoadedBundle, *CandidatePaths[Index]))
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
						TEXT("PencilExtensionResources: editor bundle containing the icon registry not found; icon widgets will fall back to icon-name text.\nInstall the Pencil VS Code extension or set the extension directory in the Design2UMG settings. Tried candidates:\n  %s"),
						*TriedList);
				}
				return;
			}

			FString ParseError;
			FString LoadedJson;
			if (!ExtractIconIndexJson(LoadedBundle, LoadedJson, ParseError)
				|| !ParseIconIndexJson(LoadedJson, State.GlyphsByFamily, State.NamesByFamily, ParseError))
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

			if (!ExtractFontStyles(LoadedBundle, TSet<FString>(), State.FontStyles, ParseError))
				UE_LOG(LogDesign2UMG, Error, TEXT("Pencil font descriptions: %s"), *ParseError);
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

	void GetLibraryPenRelativeCandidates(TArray<FString>& OutRelativeDirs)
	{
		OutRelativeDirs = {
			TEXT("out/data"),
			TEXT("node_modules/@ha/pencil-editor/data"),
		};
	}

	bool TryDeriveLibraryNameFromFileName(const FString& FileName, FString& OutLibraryName)
	{
		constexpr int32 LibPenSuffixLen = 8; // ".lib.pen"
		if (!FileName.EndsWith(TEXT(".lib.pen")) || FileName.Len() <= LibPenSuffixLen)
		{
			OutLibraryName.Reset();
			return false;
		}
		OutLibraryName = FileName.LeftChop(LibPenSuffixLen);
		return !OutLibraryName.IsEmpty();
	}

	int32 RegisterLibrariesFromDirectory(const FString& DataDir)
	{
		// "<name>.lib.pen" registers under "<name>"; the sorted order keeps registration deterministic.
		TArray<FString> LibraryFiles;
		IFileManager::Get().FindFiles(LibraryFiles, *(DataDir / TEXT("*.lib.pen")), true, false);
		LibraryFiles.Sort();

		int32 RegisteredCount = 0;
		for (const FString& LibraryFile : LibraryFiles)
		{
			FString LibraryName;
			if (!TryDeriveLibraryNameFromFileName(LibraryFile, LibraryName))
			{
				continue;
			}
			const FString LibraryPath = DataDir / LibraryFile;

			FString LibPenJson;
			TSharedPtr<FJsonObject> Library;
			const TSharedPtr<FJsonObject>* Variables = nullptr;
			if (!FFileHelper::LoadFileToString(LibPenJson, *LibraryPath)
				|| !FPenParser::ParseSourceObject(LibPenJson, Library)
				|| !Library->TryGetObjectField(TEXT("variables"), Variables) || !Variables->IsValid())
			{
				UE_LOG(LogDesign2UMG, Warning,
					TEXT("PencilExtensionResources: skipping library '%s': %s"), *LibraryPath,
					TEXT("file could not be read, parsed or has no variables object"));
				continue;
			}

			PenBundledLibraries::RegisterLibraryDocument(LibraryName, LibPenJson, LibraryPath);
			++RegisteredCount;
		}
		return RegisteredCount;
	}

	void EnsureBundledLibrariesRegistered()
	{
		FPencilResourceState& State = GetResourceState();
		if (State.bLibrariesLoaded)
		{
			return;
		}

		TArray<PencilExtensionLocator::FExtensionRoot> Candidates;
		TArray<FString> SearchedDirs;
		PencilExtensionLocator::GetExtensionRootCandidates(Candidates, SearchedDirs);

		TArray<FString> RelativeCandidates;
		GetLibraryPenRelativeCandidates(RelativeCandidates);

		TArray<FString> CandidateDirs;
		for (const PencilExtensionLocator::FExtensionRoot& Candidate : Candidates)
		{
			for (const FString& RelativeDir : RelativeCandidates)
			{
				CandidateDirs.Add(Candidate.Directory / RelativeDir);
			}
		}
		State.LastTriedLibraryDirs = CandidateDirs;

		FString DataDir;
		for (const FString& CandidateDir : CandidateDirs)
		{
			TArray<FString> LibraryFiles;
			IFileManager::Get().FindFiles(LibraryFiles, *(CandidateDir / TEXT("*.lib.pen")), true, false);
			// Skip empty layouts, but let the loader report unreadable or invalid library files.
			if (LibraryFiles.Num() > 0)
			{
				DataDir = CandidateDir;
				break;
			}
		}

		if (DataDir.IsEmpty())
		{
			if (!State.bLoggedMissingLibraries)
			{
				State.bLoggedMissingLibraries = true;
				const FString TriedList = FString::Join(State.LastTriedLibraryDirs, TEXT("\n  "));
				UE_LOG(LogDesign2UMG, Error,
					TEXT("PencilExtensionResources: bundled library documents (*.lib.pen) not found; imported library variables will not resolve.\nInstall the Pencil VS Code extension or set the extension directory in the Design2UMG settings. Tried candidates:\n  %s"),
					*TriedList);
			}
			return;
		}

		const int32 RegisteredCount = RegisterLibrariesFromDirectory(DataDir);
		if (RegisteredCount == 0)
		{
			if (!State.bLoggedMissingLibraries)
			{
				State.bLoggedMissingLibraries = true;
				UE_LOG(LogDesign2UMG, Error,
					TEXT("PencilExtensionResources: no usable library documents (*.lib.pen) in '%s'; imported library variables will not resolve."),
					*DataDir);
			}
			return;
		}

		State.bLibrariesLoaded = true;
		UE_LOG(LogDesign2UMG, Display,
			TEXT("PencilExtensionResources: registered %d library documents from '%s'"),
			RegisteredCount, *DataDir);
	}

	void ReportPenResourceGaps(FDesignDocument& Document)
	{
		FPencilResourceState& State = GetResourceState();
		if (State.bIconIndexLoaded && State.bLibrariesLoaded)
		{
			return;
		}

		bool bUsesIcons = false;
		bool bUsesLibraryVariables = false;
		if (!DocumentUsesExtensionResources(Document, bUsesIcons, bUsesLibraryVariables))
		{
			return;
		}

		TArray<FString> MissingKinds;
		TArray<FString> UsedKinds;
		if (bUsesIcons && !State.bIconIndexLoaded)
		{
			MissingKinds.Add(TEXT("icon-index-missing"));
			UsedKinds.Add(TEXT("icons"));
		}
		if (bUsesLibraryVariables && !State.bLibrariesLoaded)
		{
			MissingKinds.Add(TEXT("libraries-missing"));
			UsedKinds.Add(TEXT("library variables"));
		}
		if (MissingKinds.Num() == 0)
		{
			return;
		}

		UE_LOG(LogDesign2UMG, Warning,
			TEXT("PencilExtensionResources: pen document uses %s but the matching Pencil extension resources are not registered; icons will render as icon-name text and library variables may not resolve. Install the Pencil VS Code extension or set the extension directory in the Design2UMG settings."),
			*FString::Join(UsedKinds, TEXT(" and ")));
		Document.Metadata.Add(PenExtensionResourceGapMetadataKey, FString::Join(MissingKinds, TEXT(",")));

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
