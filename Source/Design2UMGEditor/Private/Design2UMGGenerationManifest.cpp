// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGGenerationManifest.h"

#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <atomic>

namespace
{
	constexpr int32 ManifestSchemaVersion = 1;

#if WITH_DEV_AUTOMATION_TESTS
	// Test injection: see Design2UMGGenerationManifest::ArmAutomationSidecarWriteFailureOnce.
	std::atomic<bool> bAutomationSidecarWriteFailureArmed{ false };
	std::atomic<bool> bAutomationSidecarFinalizeFailureArmed{ false };
#endif

	TSharedRef<FJsonObject> BuildMappedInstanceObject(const FDesign2UMGMappedInstanceRecord& Record)
	{
		TSharedRef<FJsonObject> Instance = MakeShared<FJsonObject>();
		Instance->SetStringField(TEXT("kind"), Design2UMGManifestKinds::Mapped);
		Instance->SetStringField(TEXT("designNodeId"), Record.DesignNodeId);
		Instance->SetStringField(TEXT("widgetName"), Record.WidgetName);
		Instance->SetStringField(TEXT("mappedKey"), Record.MappedKey);
		Instance->SetStringField(TEXT("mappedClass"), Record.MappedClass);
		if (Record.bMatchedAtSetLevel)
		{
			Instance->SetBoolField(TEXT("matchedAtSetLevel"), true);
		}

		// inputs record declarations only (source/target/valueKind). Design values never enter the manifest:
		// reimport re-converts by design identity to get fresh values instead of relying on stored property values.
		TArray<TSharedPtr<FJsonValue>> Inputs;
		for (const FDesign2UMGMappedInputRecord& Input : Record.Inputs)
		{
			TSharedRef<FJsonObject> InputObject = MakeShared<FJsonObject>();
			InputObject->SetStringField(TEXT("source"), Input.Source);
			InputObject->SetStringField(TEXT("target"), Input.Target);
			InputObject->SetStringField(TEXT("valueKind"), Input.ValueKind);
			Inputs.Add(MakeShared<FJsonValueObject>(InputObject));
		}
		Instance->SetArrayField(TEXT("inputs"), Inputs);
		return Instance;
	}
}

namespace Design2UMGGenerationManifest
{
	FString ComputeSavedAssetHash(const FString& AssetPackageName)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FPackageName::LongPackageNameToFilename(AssetPackageName, TEXT(".uasset")))) return FString();
		FSHAHash Hash;
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash);
		return Hash.ToString();
	}
	FString BuildSidecarPathForAsset(const FString& AssetPackageName)
	{
		return FPackageName::LongPackageNameToFilename(AssetPackageName, TEXT(".d2u.json"));
	}

	EDesign2UMGFileState WriteSidecarForAsset(
		const FString& AssetPackageName,
		const FDesign2UMGGenerationManifestPayload& Payload)
	{
#if WITH_DEV_AUTOMATION_TESTS
		if (bAutomationSidecarWriteFailureArmed.exchange(false))
		{
			UE_LOG(LogDesign2UMG, Error,
				TEXT("GenerationManifest: failed to write manifest %s (automation-injected sidecar write failure)"),
				*BuildSidecarPathForAsset(AssetPackageName));
			return EDesign2UMGFileState::Unchanged;
		}
#endif
		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("schemaVersion"), ManifestSchemaVersion);
		Root->SetBoolField(TEXT("componentBatchRoot"), Payload.bComponentBatchRoot);
		if (Payload.bWholeAssetOwned)
		{
			const FString Hash = ComputeSavedAssetHash(AssetPackageName);
			if (Hash.IsEmpty()) return EDesign2UMGFileState::Unchanged;
			Root->SetStringField(TEXT("generatedAssetHash"), Hash);
		}
		Root->SetStringField(TEXT("provider"), Payload.Provider);
		Root->SetStringField(TEXT("documentScope"), Payload.DocumentScope);

		TSharedRef<FJsonObject> SourceRoot = MakeShared<FJsonObject>();
		SourceRoot->SetStringField(TEXT("nodeId"), Payload.SourceRootNodeId);
		SourceRoot->SetStringField(TEXT("name"), Payload.SourceRootName);
		Root->SetObjectField(TEXT("sourceRoot"), SourceRoot);

		TArray<TSharedPtr<FJsonValue>> Instances;
		for (const FDesign2UMGMappedInstanceRecord& Record : Payload.MappedInstances)
		{
			Instances.Add(MakeShared<FJsonValueObject>(BuildMappedInstanceObject(Record)));
		}
		for (const FDesign2UMGGeneratedInstanceRecord& Record : Payload.TopLevelGenerated)
		{
			TSharedRef<FJsonObject> Instance = MakeShared<FJsonObject>();
			Instance->SetStringField(TEXT("kind"), Design2UMGManifestKinds::Generated);
			Instance->SetStringField(TEXT("designNodeId"), Record.DesignNodeId);
			Instance->SetStringField(TEXT("widgetName"), Record.WidgetName);
			Instances.Add(MakeShared<FJsonValueObject>(Instance));
		}
		Root->SetArrayField(TEXT("instances"), Instances);

		// d2uOwned is given explicitly by the caller (import = every widget name created this run; reimport = the updated full set).
		TArray<TSharedPtr<FJsonValue>> D2UOwned;
		for (const FString& OwnedName : Payload.D2UOwnedNames)
		{
			D2UOwned.Add(MakeShared<FJsonValueString>(OwnedName));
		}
		Root->SetArrayField(TEXT("d2uOwned"), D2UOwned);

		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		if (!FJsonSerializer::Serialize(Root, Writer))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("GenerationManifest: failed to serialize manifest for %s"), *AssetPackageName);
			return EDesign2UMGFileState::Unchanged;
		}

		const FString SidecarPath = BuildSidecarPathForAsset(AssetPackageName);
		const FString TempPath = SidecarPath + TEXT(".tmp");
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(SidecarPath), true);
		if (!FFileHelper::SaveStringToFile(Json, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("GenerationManifest: failed to write temp manifest %s"), *TempPath);
			IFileManager::Get().Delete(*TempPath, false, true);
			return EDesign2UMGFileState::Unchanged;
		}

#if WITH_DEV_AUTOMATION_TESTS
		if (bAutomationSidecarFinalizeFailureArmed.exchange(false)) IFileManager::Get().Delete(*TempPath, false, true);
#endif
		// Move may delete the destination before it fails; never promise the previous sidecar survived.
		if (!IFileManager::Get().Move(*SidecarPath, *TempPath, true))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("GenerationManifest: failed to finalize manifest %s"), *SidecarPath);
			IFileManager::Get().Delete(*TempPath, false, true);
			return EDesign2UMGFileState::Indeterminate;
		}

		UE_LOG(LogDesign2UMG, Log, TEXT("GenerationManifest: wrote %s"), *SidecarPath);
		return EDesign2UMGFileState::Saved;
	}

#if WITH_DEV_AUTOMATION_TESTS
	void ArmAutomationSidecarWriteFailureOnce()
	{
		bAutomationSidecarWriteFailureArmed.store(true);
	}

	void ArmAutomationSidecarFinalizeFailureOnce()
	{
		bAutomationSidecarFinalizeFailureArmed.store(true);
	}
#endif

	bool TryLoadSidecarForAsset(
		const FString& AssetPackageName,
		FDesign2UMGGenerationManifestSidecar& OutSidecar,
		FString& OutError)
	{
		OutSidecar = FDesign2UMGGenerationManifestSidecar();
		const FString SidecarPath = BuildSidecarPathForAsset(AssetPackageName);
		if (!FPaths::FileExists(SidecarPath))
		{
			OutError = FString::Printf(TEXT("generation manifest sidecar not found: %s"), *SidecarPath);
			return false;
		}

		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *SidecarPath))
		{
			OutError = FString::Printf(TEXT("generation manifest sidecar not readable: %s"), *SidecarPath);
			return false;
		}

		TSharedPtr<FJsonObject> Root;
		if (!DesignJsonUtils::TryParseObject(Json, Root))
		{
			OutError = FString::Printf(TEXT("generation manifest sidecar is not valid JSON: %s"), *SidecarPath);
			return false;
		}

		OutSidecar.SchemaVersion = static_cast<int32>(Root->GetNumberField(TEXT("schemaVersion")));
		Root->TryGetStringField(TEXT("generatedAssetHash"), OutSidecar.GeneratedAssetHash);
		Root->TryGetBoolField(TEXT("componentBatchRoot"), OutSidecar.bComponentBatchRoot);
		if (OutSidecar.SchemaVersion != ManifestSchemaVersion)
		{
			OutError = FString::Printf(
				TEXT("generation manifest schema version %d is not supported (expected %d): %s"),
				OutSidecar.SchemaVersion, ManifestSchemaVersion, *SidecarPath);
			return false;
		}

		Root->TryGetStringField(TEXT("provider"), OutSidecar.Provider);
		Root->TryGetStringField(TEXT("documentScope"), OutSidecar.DocumentScope);
		if (OutSidecar.Provider.IsEmpty() || OutSidecar.DocumentScope.IsEmpty())
		{
			OutError = FString::Printf(TEXT("generation manifest is missing provider/documentScope: %s"), *SidecarPath);
			return false;
		}

		const TSharedPtr<FJsonObject>* SourceRoot = nullptr;
		if (Root->TryGetObjectField(TEXT("sourceRoot"), SourceRoot) && SourceRoot && (*SourceRoot).IsValid())
		{
			(*SourceRoot)->TryGetStringField(TEXT("nodeId"), OutSidecar.SourceRootNodeId);
			(*SourceRoot)->TryGetStringField(TEXT("name"), OutSidecar.SourceRootName);
		}
		// sourceRoot.nodeId may be empty: interactive documents synthesize an identity-less root during conversion
		// (switcher/overlay hosts); reimport then uses the new IR root as the scope (conversion of the same document is deterministic).

		const TArray<TSharedPtr<FJsonValue>>* Instances = nullptr;
		TSet<FString> DesignIds;
		TSet<FString> WidgetNames;
		if (Root->TryGetArrayField(TEXT("instances"), Instances) && Instances)
		{
			for (const TSharedPtr<FJsonValue>& Value : *Instances)
			{
				const TSharedPtr<FJsonObject> Instance = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
				if (!Instance.IsValid())
				{
					OutError = TEXT("generation manifest contains a non-object instance: ") + SidecarPath;
					return false;
				}

				FDesign2UMGGenerationManifestSidecar::FInstanceRecord Record;
				Instance->TryGetStringField(TEXT("kind"), Record.Kind);
				Instance->TryGetStringField(TEXT("designNodeId"), Record.DesignNodeId);
				Instance->TryGetStringField(TEXT("widgetName"), Record.WidgetName);
				Instance->TryGetStringField(TEXT("mappedKey"), Record.MappedKey);
				Instance->TryGetStringField(TEXT("mappedClass"), Record.MappedClass);
				Instance->TryGetBoolField(TEXT("matchedAtSetLevel"), Record.bMatchedAtSetLevel);
				if ((Record.Kind != Design2UMGManifestKinds::Generated && Record.Kind != Design2UMGManifestKinds::Mapped)
					|| Record.DesignNodeId.IsEmpty() || Record.WidgetName.IsEmpty()
					|| DesignIds.Contains(Record.DesignNodeId) || WidgetNames.Contains(Record.WidgetName)
					|| (Record.Kind == Design2UMGManifestKinds::Mapped && (Record.MappedKey.IsEmpty() || Record.MappedClass.IsEmpty())))
				{
					OutError = TEXT("generation manifest contains an invalid or duplicate instance identity: ") + SidecarPath;
					return false;
				}
				DesignIds.Add(Record.DesignNodeId);
				WidgetNames.Add(Record.WidgetName);

				// inputs are read back as declarations only; a value field from historical versions is ignored per the schema consolidation rule.
				const TArray<TSharedPtr<FJsonValue>>* Inputs = nullptr;
				if (Instance->TryGetArrayField(TEXT("inputs"), Inputs) && Inputs)
				{
					for (const TSharedPtr<FJsonValue>& InputValue : *Inputs)
					{
						const TSharedPtr<FJsonObject> InputObject = InputValue->AsObject();
						if (!InputObject.IsValid())
						{
							continue;
						}
						FDesign2UMGMappedInputRecord InputRecord;
						InputObject->TryGetStringField(TEXT("source"), InputRecord.Source);
						InputObject->TryGetStringField(TEXT("target"), InputRecord.Target);
						InputObject->TryGetStringField(TEXT("valueKind"), InputRecord.ValueKind);
						Record.Inputs.Add(MoveTemp(InputRecord));
					}
				}
				OutSidecar.Instances.Add(MoveTemp(Record));
			}
		}
		if (OutSidecar.Instances.Num() == 0)
		{
			OutError = FString::Printf(TEXT("generation manifest records no instances: %s"), *SidecarPath);
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* D2UOwned = nullptr;
		if (Root->TryGetArrayField(TEXT("d2uOwned"), D2UOwned) && D2UOwned)
		{
			for (const TSharedPtr<FJsonValue>& OwnedValue : *D2UOwned)
			{
				FString Name;
				if (!OwnedValue.IsValid() || !OwnedValue->TryGetString(Name) || Name.IsEmpty() || OutSidecar.D2UOwned.Contains(Name))
				{
					OutError = TEXT("generation manifest contains an invalid or duplicate owned widget: ") + SidecarPath;
					return false;
				}
				OutSidecar.D2UOwned.Add(Name);
			}
		}
		for (const FString& Name : WidgetNames)
		{
			if (!OutSidecar.D2UOwned.Contains(Name))
			{
				OutError = TEXT("generation manifest instance is absent from owned widgets: ") + Name;
				return false;
			}
		}
		return true;
	}
}
