// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGComponentMappingConfig.h"

#include "Blueprint/UserWidget.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

namespace
{
	const TCHAR* MappingSection = TEXT("/Design2UMG/ComponentMapping");
	const TCHAR* MappingArrayKey = TEXT("Mappings");

	void AddDiagnostic(TArray<FDesign2UMGMappingDiagnostic>& Out, const FString& Kind, const FString& Message)
	{
		FDesign2UMGMappingDiagnostic Diagnostic;
		Diagnostic.Kind = Kind;
		Diagnostic.Message = Message;
		Out.Add(MoveTemp(Diagnostic));
	}

	// Parse each "(Key=...,WidgetClass=...,Inputs=((...),...))" line into a struct via the UE property system;
	// nested arrays are handled by ImportText too - no hand-written ini value parser.
	bool ParseEntryLine(const FString& Line, FDesignComponentMappingEntryLine& OutEntry)
	{
		if (!Line.StartsWith(TEXT("(")))
		{
			return false;
		}
		const UScriptStruct* Struct = FDesignComponentMappingEntryLine::StaticStruct();
		const TCHAR* Buffer = *Line;
		return Struct->ImportText(Buffer, &OutEntry, nullptr, PPF_None, nullptr, Struct->GetName()) != nullptr;
	}
}

namespace Design2UMGComponentMappingConfig
{
	FDesign2UMGComponentMappingConfig Load(const FString& IniPathOverride)
	{
		FDesign2UMGComponentMappingConfig Result;

		const FString IniPath = !IniPathOverride.IsEmpty()
			? IniPathOverride
			: FPaths::ProjectConfigDir() / TEXT("Design2UMGComponentMapping.ini");
		if (!FPaths::FileExists(IniPath))
		{
			// Missing ini = no project mapping configured; unmapped nodes keep the existing conversion path. Stay silent.
			return Result;
		}

		FConfigFile ConfigFile;
		FString IniText;
		if (!FFileHelper::LoadFileToString(IniText, *IniPath))
		{
			// Ini present but the whole file fails to read (permissions/exclusive lock/IO error) is NOT "unconfigured": silently
			// returning an empty table would send every node of a configured project down the plain conversion path. Diagnostics must distinguish this from unconfigured.
			AddDiagnostic(
				Result.ValidationDiagnostics,
				TEXT("mapping_config_invalid"),
				FString::Printf(
					TEXT("Mapping ini exists but could not be read (locked or IO error): %s; no mappings are active"),
					*IniPath));
			return Result;
		}
		// CombineFromBuffer goes through full ini symbol semantics (including +Mappings array-append lines);
		// FConfigFile::Read must not be used: it ignores +/- command prefixes.
		ConfigFile.CombineFromBuffer(IniText, IniPath);
		TArray<FString> Lines;
		ConfigFile.GetArray(MappingSection, MappingArrayKey, Lines);

		for (const FString& Line : Lines)
		{
			FDesignComponentMappingEntryLine EntryLine;
			if (!ParseEntryLine(Line, EntryLine))
			{
				AddDiagnostic(
					Result.ValidationDiagnostics,
					TEXT("mapping_config_invalid"),
					FString::Printf(TEXT("Mapping entry could not be parsed by the config system: %s"), *Line));
				continue;
			}

			FString Provider;
			FString DocumentScope;
			FString ComponentKey;
			if (!DesignComponentMapping::ParseMappingKey(EntryLine.Key, Provider, DocumentScope, ComponentKey))
			{
				AddDiagnostic(
					Result.ValidationDiagnostics,
					TEXT("mapping_config_invalid"),
					FString::Printf(
						TEXT("Mapping Key '%s' must be three non-empty '|' separated segments: Provider|DocumentScope|ComponentKey"),
						*EntryLine.Key));
				continue;
			}

			if (!Provider.Equals(DesignComponentMapping::ProviderFigma, ESearchCase::IgnoreCase))
			{
				AddDiagnostic(
					Result.ValidationDiagnostics,
					TEXT("mapping_config_invalid"),
					FString::Printf(
						TEXT("Mapping '%s' dropped: provider must be 'figma' in v1 (Pencil mapping is not enabled)"),
						*EntryLine.Key));
				continue;
			}

			if (EntryLine.WidgetClass.IsEmpty())
			{
				AddDiagnostic(
					Result.ValidationDiagnostics,
					TEXT("mapping_config_invalid"),
					FString::Printf(TEXT("Mapping '%s' dropped: WidgetClass is empty"), *EntryLine.Key));
				continue;
			}

			// Load the UClass first, then judge the type: TryLoadClass<UUserWidget> collapses "loadable but not a
			// UUserWidget subclass" into null as well, misreporting type mismatches as load failures.
			const FSoftClassPath WidgetClassPath(EntryLine.WidgetClass);
			UClass* LoadedClass = Cast<UClass>(WidgetClassPath.TryLoad());
			if (!LoadedClass)
			{
				AddDiagnostic(
					Result.ValidationDiagnostics,
					TEXT("mapping_class_unavailable"),
					FString::Printf(
						TEXT("Mapping '%s' dropped: WidgetClass '%s' could not be loaded; nodes matching it fall back to normal conversion"),
						*EntryLine.Key,
						*EntryLine.WidgetClass));
				continue;
			}
			if (!LoadedClass->IsChildOf(UUserWidget::StaticClass()))
			{
				AddDiagnostic(
					Result.ValidationDiagnostics,
					TEXT("mapping_class_unavailable"),
					FString::Printf(
						TEXT("Mapping '%s' dropped: WidgetClass '%s' is not a UUserWidget subclass"),
						*EntryLine.Key,
						*EntryLine.WidgetClass));
				continue;
			}

			DesignComponentMapping::FComponentMappingEntry Entry;
			Entry.Key = EntryLine.Key;
			Entry.Provider = Provider;
			Entry.DocumentScope = DocumentScope;
			Entry.ComponentKey = ComponentKey;
			Entry.WidgetClass = EntryLine.WidgetClass;

			for (const FDesignComponentMappingInputLine& InputLine : EntryLine.Inputs)
			{
				DesignComponentMapping::FComponentInputDeclaration Declaration;
				FString InputError;
				if (DesignComponentMapping::TryParseComponentInputDeclaration(
					InputLine.Source,
					InputLine.Target,
					InputLine.Type,
					InputLine.ValueKind,
					Declaration,
					InputError))
				{
					Entry.Inputs.Add(MoveTemp(Declaration));
					continue;
				}
				AddDiagnostic(
					Result.ValidationDiagnostics,
					TEXT("mapping_input_invalid"),
					FString::Printf(
						TEXT("Mapping '%s' input (Source='%s', Target='%s', Type='%s') skipped: %s"),
						*EntryLine.Key,
						*InputLine.Source,
						*InputLine.Target,
						*InputLine.Type,
						*InputError));
			}

			if (Result.Table.FindEntryByKey(Entry.Key) != nullptr)
			{
				AddDiagnostic(
					Result.ValidationDiagnostics,
					TEXT("mapping_config_invalid"),
					FString::Printf(
						TEXT("Mapping '%s' dropped: a mapping with this Key already exists; the first entry wins"),
						*EntryLine.Key));
				continue;
			}

			Result.Table.Entries.Add(MoveTemp(Entry));
		}

		return Result;
	}
}
