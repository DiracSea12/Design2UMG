// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportPipeline.h"
#include "FigmaHttpClient.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/ThreadSafeCounter.h"
#include "PencilConnector.h"

struct FDesignDocument;

namespace Design2UMGEditorImportServices
{
	class FFigmaImportService;

	class FFigmaImportSession : public TSharedFromThis<FFigmaImportSession>
	{
	public:
		FFigmaImportSession(
			FFigmaImportService& InOwner,
			FString InFileKey,
			const FString& Token,
			const uint32 InGeneration,
			const FDesign2UMGImportSettingsSnapshot& InSettingsSnapshot)
			: Owner(InOwner)
			, FileKey(MoveTemp(InFileKey))
			, Generation(InGeneration)
			, Client(MakeUnique<FFigmaHttpClient>())
			, SettingsSnapshot(InSettingsSnapshot)
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
		void HandleImagesResolved(const TSharedRef<FDesignDocument>& Document, const bool bImagesResolved);
		void HandleVectorsExported(
			const TSharedRef<FDesignDocument>& Document,
			const bool bVectorsExported,
			const TMap<FString, FString>& IdToUrl);
		bool IsCancelled() const;

		FFigmaImportService& Owner;
		FString FileKey;
		uint32 Generation = 0;
		TUniquePtr<FFigmaHttpClient> Client;
		FDesign2UMGImportSettingsSnapshot SettingsSnapshot;
	};

	class FFigmaImportService
	{
	public:
		void RunImport(
			const FString& FileKey,
			const FString& Token,
			const FDesign2UMGImportSettingsSnapshot& SettingsSnapshot)
		{
			Invalidate();
			const uint32 ImportGeneration = static_cast<uint32>(Generation.GetValue());
			const TSharedRef<FFigmaImportSession> Session = MakeShared<FFigmaImportSession>(
				*this,
				FileKey,
				Token,
				ImportGeneration,
				SettingsSnapshot);
			ActiveSession = Session;
			Session->Start();
		}

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
