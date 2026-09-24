// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportPipeline.h"
#include "FigmaHttpClient.h"
#include "DesignImageDiagnostics.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/ThreadSafeCounter.h"
#include "PencilConnector.h"

struct FDesignDocument;
class FJsonObject;

namespace Design2UMGEditorImportServices
{
	enum class EFigmaImportCompletionKind : uint8
	{
		Success,
		Warnings,
		ImageFailure
	};

	EFigmaImportCompletionKind ClassifyFigmaImportCompletion(
		const FUMGWidgetData& Data,
		const FDesignImageImportResult& ImageResult);

	class FFigmaImportService;

	class FFigmaImportSession : public TSharedFromThis<FFigmaImportSession>
	{
	public:
		using FOnResolvedDocument = TFunction<void(const TSharedRef<FDesignDocument>&, bool)>;

		FFigmaImportSession(
			FFigmaImportService& InOwner,
			FString InFileKey,
			const FString& Token,
			const uint32 InGeneration,
			const FDesign2UMGImportSettingsSnapshot& InSettingsSnapshot,
			FOnResolvedDocument InOnResolvedDocument,
			FString InSelectedRootId = FString(),
			FHttpModule& HttpModule = FHttpModule::Get())
			: Owner(InOwner)
			, FileKey(MoveTemp(InFileKey))
			, SelectedRootId(MoveTemp(InSelectedRootId))
			, Generation(InGeneration)
			, Client(MakeUnique<FFigmaHttpClient>(HttpModule))
			, SettingsSnapshot(InSettingsSnapshot)
			, OnResolvedDocument(MoveTemp(InOnResolvedDocument))
		{
			Client->SetToken(Token);
		}

		~FFigmaImportSession()
		{
			CancelRequests();
		}

		void Start();
		void CancelRequests();

	private:
		void HandleFileResponse(const bool bSuccess, const FString& Body);
		void SelectImportRootAndResolve(const FString& PreparedSource);
		void BeginResolveSelectedRoot(const FString& RootId);
		void ResolveMissingComponentDefinitions();
		void ParseResolvedComponentFile();
		void RecordDependencyFailure(const FString& Key, const FString& Reason);
		void FailComponentImport(const FString& Reason);
		TSharedPtr<FJsonObject> ComponentSource;
		TSet<FString> AttemptedComponentKeys;
		TSet<FString> ResolvedLibraryFiles;
		TMap<FString, FString> ComponentImageUrls;
		void HandleImagesResolved(const TSharedRef<FDesignDocument>& Document, const bool bImagesResolved);
		void HandleVectorsExported(
			const TSharedRef<FDesignDocument>& Document,
			const bool bVectorsExported,
			const TMap<FString, FString>& IdToUrl);
		bool IsCancelled() const;

		// Carries image-reference resolution failure into the final import result.
		bool bImageResolutionFailed = false;

		FFigmaImportService& Owner;
		FString FileKey;
		FString SelectedRootId;
		uint32 Generation = 0;
		TUniquePtr<FFigmaHttpClient> Client;
		FDesign2UMGImportSettingsSnapshot SettingsSnapshot;
		FOnResolvedDocument OnResolvedDocument;
	};

	class FFigmaImportService
	{
	public:
		void RunImport(
			const FString& FileKey,
			const FString& Token,
			const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot);

		void Invalidate()
		{
			Generation.Increment();
			if (ActiveSession.IsValid())
			{
				ActiveSession.Pin()->CancelRequests();
				ActiveSession.Reset();
			}
		}

		bool IsGenerationCurrent(const uint32 Token) const
		{
			return static_cast<uint32>(Generation.GetValue()) == Token;
		}

	private:
		FThreadSafeCounter Generation{ 1 };
		TWeakPtr<FFigmaImportSession> ActiveSession;
	};

	class FPencilImportService
	{
	public:
		void ConnectAndImport();
		void Shutdown()
		{
			Generation.Increment();
			ReleaseFetchSlot();

			if (Connector)
			{
				Connector->GetClient().CancelRequests();
				Connector->Shutdown();
				Connector.Reset();
			}
		}

	private:
		void BeginFetch(const uint32 FetchGeneration);

		FPencilConnector& GetOrCreateConnector()
		{
			if (!Connector)
			{
				Connector = MakeUnique<FPencilConnector>();
			}

			return *Connector;
		}

		bool IsGenerationCurrent(const uint32 Token) const
		{
			return static_cast<uint32>(Generation.GetValue()) == Token;
		}

		void ReleaseFetchSlot()
		{
			FPlatformAtomics::InterlockedExchange(&bFetchInProgress, 0);
		}

		TUniquePtr<FPencilConnector> Connector;
		volatile int32 bFetchInProgress{ 0 };
		FThreadSafeCounter Generation{ 1 };
	};
}
