// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Design2UMGEditorImportServices.h"
#include "Design2UMGLog.h"
#include "FigmaComponentDependencies.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace Design2UMGEditorImportServices
{
	void FFigmaImportSession::RecordDependencyFailure(const FString& Key, const FString& Reason)
	{
		const auto Bundle = ComponentSource->GetObjectField(TEXT("design2umgComponentDependencies"));
		TArray<TSharedPtr<FJsonValue>> Errors = Bundle->GetArrayField(TEXT("errors"));
		TSharedRef<FJsonObject> Error = MakeShared<FJsonObject>();
		Error->SetStringField(TEXT("kind"), TEXT("component_source_unavailable"));
		Error->SetStringField(TEXT("nodeId"), Key);
		Error->SetStringField(TEXT("message"), Reason);
		Errors.Add(MakeShared<FJsonValueObject>(Error));
		Bundle->SetArrayField(TEXT("errors"), Errors);
		UE_LOG(LogDesign2UMG, Warning, TEXT("Component '%s': %s"), *Key, *Reason);
	}

	void FFigmaImportSession::ResolveMissingComponentDefinitions()
	{
		if (IsCancelled()) return;
		const auto Bundle = ComponentSource->GetObjectField(TEXT("design2umgComponentDependencies"));
		FString BundleJson, Error;
		FJsonSerializer::Serialize(Bundle.ToSharedRef(), TJsonWriterFactory<>::Create(&BundleJson));
		FigmaComponentDependencies::FCatalog Catalog;
		if (!FigmaComponentDependencies::Parse(BundleJson, Catalog, Error))
		{
			FailComponentImport(TEXT("Invalid Figma component catalogue: ") + Error);
			return;
		}
		FString MissingKey;
		TSet<FString> Visited;
		auto IsMapped = [&](const FString& Key)
		{
			DesignComponentMapping::FDesignComponentIdentity Identity;
			Identity.Provider = TEXT("figma");
			Identity.DocumentScope = FileKey;
			Identity.ComponentKey = Key;
		if (const auto* Definition = Catalog.Definitions.Find(Key)) Identity.ComponentSetKey = Definition->ComponentSetKey;
		if (Identity.ComponentSetKey.IsEmpty())
		{
			const TSharedPtr<FJsonObject>* Components = nullptr;
			const TSharedPtr<FJsonObject>* Sets = nullptr;
			if (ComponentSource->TryGetObjectField(TEXT("components"), Components)
				&& ComponentSource->TryGetObjectField(TEXT("componentSets"), Sets))
			{
				for (const auto& Pair : (*Components)->Values)
				{
					if (Pair.Value->Type != EJson::Object) continue;
					const auto Component = Pair.Value->AsObject();
					if (DesignJsonUtils::GetString(Component, TEXT("key")) != Key) continue;
					const TSharedPtr<FJsonObject>* Set = nullptr;
					if ((*Sets)->TryGetObjectField(DesignJsonUtils::GetString(Component, TEXT("componentSetId")), Set))
						Identity.ComponentSetKey = DesignJsonUtils::GetString(*Set, TEXT("key"));
					break;
				}
			}
		}
			bool AtSet = false;
			return SettingsSnapshot.GenerationSettings.ComponentMapping.Table.Match(Identity, AtSet) != nullptr;
		};
		TArray<const FigmaComponentDependencies::FReference*> Pending;
		auto AddRefs = [&](const TArray<FigmaComponentDependencies::FReference>& References)
		{
			for (const auto& Reference : References) Pending.Add(&Reference);
		};
		auto Visit = [&](const FString& Key)
		{
			if (Visited.Contains(Key) || !MissingKey.IsEmpty()) return;
			Visited.Add(Key);
			const auto* Definition = Catalog.Definitions.Find(Key);
			if (IsMapped(Key)) return;
			if (Definition) AddRefs(Definition->References);
			else if (!AttemptedComponentKeys.Contains(Key)) MissingKey = Key;
		};
		for (const auto& Instance : Catalog.InstanceKeys) Visit(Instance.Value);
		while (!Pending.IsEmpty() && MissingKey.IsEmpty())
		{
			const auto* Reference = Pending.Pop(EAllowShrinking::No);
			if (IsMapped(Reference->Key)) continue;
			Visit(Reference->Key);
			AddRefs(Reference->Children);
		}
		if (MissingKey.IsEmpty())
		{
			ParseResolvedComponentFile();
			return;
		}
		AttemptedComponentKeys.Add(MissingKey);
		Client->GetComponent(MissingKey, [Self = AsShared(), MissingKey](bool Success, const FString& Body)
		{
			if (Self->IsCancelled()) return;
			TSharedPtr<FJsonObject> Json;
			const TSharedPtr<FJsonObject>* Meta = nullptr;
			FString Library;
			if (Success && DesignJsonUtils::TryParseObject(Body, Json) && Json->TryGetObjectField(TEXT("meta"), Meta))
				Library = DesignJsonUtils::GetString(*Meta, TEXT("file_key"));
			if (Library.IsEmpty() || Self->ResolvedLibraryFiles.Contains(Library))
			{
				Self->RecordDependencyFailure(MissingKey, TEXT("Published component lookup failed or source file did not contain the requested definition"));
				Self->ResolveMissingComponentDefinitions();
				return;
			}
			Self->ResolvedLibraryFiles.Add(Library);
			Self->Client->GetFile(Library, [Self, MissingKey, Library](bool FileSuccess, const FString& FileBody)
			{
				if (Self->IsCancelled()) return;
				FString Prepared, Reason;
				TSharedPtr<FJsonObject> LibraryRoot;
				if (!FileSuccess || !FigmaComponentDependencies::PrepareRestFile(FileBody, Library, Prepared, Reason)
					|| !DesignJsonUtils::TryParseObject(Prepared, LibraryRoot))
				{
					Self->RecordDependencyFailure(MissingKey, TEXT("Failed to read component source file: ") + Library + TEXT(" ") + Reason);
					Self->ResolveMissingComponentDefinitions();
					return;
				}
				const auto TargetBundle = Self->ComponentSource->GetObjectField(TEXT("design2umgComponentDependencies"));
				TArray<TSharedPtr<FJsonValue>> Definitions = TargetBundle->GetArrayField(TEXT("definitions"));
				TSet<FString> Keys;
				for (const auto& Definition : Definitions) Keys.Add(DesignJsonUtils::GetString(Definition->AsObject(), TEXT("key")));
				for (const auto& Definition : LibraryRoot->GetObjectField(TEXT("design2umgComponentDependencies"))->GetArrayField(TEXT("definitions")))
				{
					const FString Key = DesignJsonUtils::GetString(Definition->AsObject(), TEXT("key"));
					if (!Keys.Contains(Key)) { Definitions.Add(Definition); Keys.Add(Key); }
				}
				TargetBundle->SetArrayField(TEXT("definitions"), Definitions);
				Self->Client->GetFileImages(Library, [Self](bool ImagesSuccess, const FString& ImagesBody)
				{
					if (Self->IsCancelled()) return;
					TSharedPtr<FJsonObject> ImagesJson;
					const TSharedPtr<FJsonObject>* ImagesMeta = nullptr;
					const TSharedPtr<FJsonObject>* Images = nullptr;
					if (ImagesSuccess && DesignJsonUtils::TryParseObject(ImagesBody, ImagesJson)
						&& ImagesJson->TryGetObjectField(TEXT("meta"), ImagesMeta) && (*ImagesMeta)->TryGetObjectField(TEXT("images"), Images))
					{
						for (const auto& Image : (*Images)->Values)
						{
							FString Url;
							if (Image.Value->TryGetString(Url)) Self->ComponentImageUrls.Add(FString(Image.Key), Url);
						}
					}
					Self->ResolveMissingComponentDefinitions();
				});
			});
		});
	}
}
