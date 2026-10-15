// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/OutputDevice.h"

#if UE_VERSION_OLDER_THAN(5, 2, 0)
#include "Misc/FeedbackContext.h"
#include "Misc/ScopeLock.h"
#endif

namespace Design2UMGAutomationTestAdapter
{
	enum class EMessageMatch : uint8
	{
		Exact,
		Contains
	};

#if UE_VERSION_OLDER_THAN(5, 4, 0)
	inline FString MakeCaseInsensitiveLiteralPattern(const FString& Literal)
	{
		FString Escaped;
		Escaped.Reserve(Literal.Len() * 2);
		for (const TCHAR Character : Literal)
		{
			switch (Character)
			{
			case TEXT('\\'): case TEXT('.'): case TEXT('^'): case TEXT('$'): case TEXT('|'):
			case TEXT('('): case TEXT(')'): case TEXT('['): case TEXT(']'): case TEXT('{'):
			case TEXT('}'): case TEXT('*'): case TEXT('+'): case TEXT('?'):
				Escaped.AppendChar(TEXT('\\'));
				break;
			default:
				break;
			}
			Escaped.AppendChar(Character);
		}
		return FString::Printf(TEXT("(?i:%s)"), *Escaped);
	}
#endif

#if UE_VERSION_OLDER_THAN(5, 2, 0)
	class FScopedExpectedMessages final : public FFeedbackContext
#else
	class FScopedExpectedMessages final : public FOutputDevice
#endif
	{
#if UE_VERSION_OLDER_THAN(5, 2, 0)
		struct FExpectedMessage
		{
			FString Text;
			ELogVerbosity::Type Verbosity = ELogVerbosity::NoLogging;
			EMessageMatch Match = EMessageMatch::Contains;
			int32 Occurrences = 1;
			int32 ObservedWarningOccurrences = 0;
			int32 ObservedErrorOccurrences = 0;
		};
#endif

	public:
		explicit FScopedExpectedMessages(FAutomationTestBase& InTest)
			: Test(InTest)
#if UE_VERSION_OLDER_THAN(5, 2, 0)
			, ForwardingContext(GWarn)
#endif
		{
#if UE_VERSION_OLDER_THAN(5, 2, 0)
			check(ForwardingContext);
			TreatWarningsAsErrors = ForwardingContext->TreatWarningsAsErrors;
			GWarn = this;
#endif
		}

		~FScopedExpectedMessages()
		{
#if UE_VERSION_OLDER_THAN(5, 2, 0)
			ForwardingContext->TreatWarningsAsErrors = TreatWarningsAsErrors;
			GWarn = ForwardingContext;

			FScopeLock Lock(&ObservationCS);
			for (const FExpectedMessage& Expected : Expectations)
			{
				const int32 ObservedOccurrences = Expected.ObservedWarningOccurrences + Expected.ObservedErrorOccurrences;
				const bool bExpectedWarning = Expected.Verbosity == ELogVerbosity::Warning;
				const int32 ExpectedVerbosityOccurrences = bExpectedWarning ? Expected.ObservedWarningOccurrences : Expected.ObservedErrorOccurrences;
				const int32 UnexpectedVerbosityOccurrences = bExpectedWarning ? Expected.ObservedErrorOccurrences : Expected.ObservedWarningOccurrences;
				const FString MessageWhat = FString::Printf(TEXT("%s message matching '%s'"),
					bExpectedWarning ? TEXT("Warning") : TEXT("Error"), *Expected.Text);
				const bool bTotalMatches = Expected.Occurrences == 0 ? ObservedOccurrences > 0 : ObservedOccurrences == Expected.Occurrences;
				const bool bVerbosityMatches = Expected.Occurrences == 0 ? ExpectedVerbosityOccurrences > 0 : ExpectedVerbosityOccurrences == Expected.Occurrences;
				if (!bTotalMatches || !bVerbosityMatches || UnexpectedVerbosityOccurrences != 0)
				{
					const FString ExpectedCount = Expected.Occurrences == 0 ? TEXT("at least one") : FString::FromInt(Expected.Occurrences);
					Test.AddEvent(FAutomationEvent(EAutomationEventType::Error, FString::Printf(TEXT(
						"Expected %s occurrence(s) for %s; observed raw total %d, expected verbosity %d, opposite verbosity %d"),
						*ExpectedCount, *MessageWhat, ObservedOccurrences, ExpectedVerbosityOccurrences, UnexpectedVerbosityOccurrences)));
				}
			}
#endif
		}

		void Expect(FString Text, ELogVerbosity::Type Verbosity, EMessageMatch Match = EMessageMatch::Contains,
			int32 Occurrences = 1)
		{
#if UE_VERSION_OLDER_THAN(5, 2, 0)
			{
				FScopeLock Lock(&ObservationCS);
				FExpectedMessage& Expected = Expectations.Emplace_GetRef();
				Expected.Text = Text;
				Expected.Verbosity = Verbosity;
				Expected.Match = Match;
				Expected.Occurrences = Occurrences;
			}
			Test.AddExpectedError(MakeCaseInsensitiveLiteralPattern(Text),
				Match == EMessageMatch::Exact ? EAutomationExpectedErrorFlags::Exact : EAutomationExpectedErrorFlags::Contains,
				Occurrences);
#elif UE_VERSION_OLDER_THAN(5, 4, 0)
			Test.AddExpectedMessage(MakeCaseInsensitiveLiteralPattern(Text), Verbosity,
				Match == EMessageMatch::Exact ? EAutomationExpectedMessageFlags::Exact : EAutomationExpectedMessageFlags::Contains,
				Occurrences);
#else
			Test.AddExpectedMessagePlain(MoveTemp(Text), Verbosity,
				Match == EMessageMatch::Exact ? EAutomationExpectedMessageFlags::Exact : EAutomationExpectedMessageFlags::Contains,
				Occurrences);
#endif
		}

#if UE_VERSION_OLDER_THAN(5, 2, 0)
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (V)
			{
				const ELogVerbosity::Type MessageVerbosity = static_cast<ELogVerbosity::Type>(
					Verbosity & ELogVerbosity::VerbosityMask);
				if (MessageVerbosity == ELogVerbosity::Warning || MessageVerbosity == ELogVerbosity::Error)
				{
					const FString Message(V);
					FScopeLock Lock(&ObservationCS);
					for (FExpectedMessage& Expected : Expectations)
					{
						const bool bTextMatches = Expected.Match == EMessageMatch::Exact
							? Message.Equals(Expected.Text, ESearchCase::IgnoreCase)
							: Message.Contains(Expected.Text, ESearchCase::IgnoreCase);
						if (bTextMatches)
						{
							int32& ObservedOccurrences = MessageVerbosity == ELogVerbosity::Warning
								? Expected.ObservedWarningOccurrences : Expected.ObservedErrorOccurrences;
							++ObservedOccurrences;
						}
					}
				}
			}
			ForwardingContext->Serialize(V, Verbosity, Category);
		}

		virtual bool CanBeUsedOnMultipleThreads() const override
		{
			return ForwardingContext->CanBeUsedOnMultipleThreads();
		}
#else
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName&) override
		{
		}
#endif

	private:
		FAutomationTestBase& Test;
#if UE_VERSION_OLDER_THAN(5, 2, 0)
		TArray<FExpectedMessage> Expectations;
		FCriticalSection ObservationCS;
		FFeedbackContext* ForwardingContext;
#endif
	};
	inline void AddExpectedErrorPlain(FAutomationTestBase& Test, FString ExpectedString,
		EAutomationExpectedErrorFlags::MatchType CompareType = EAutomationExpectedErrorFlags::Contains,
		int32 Occurrences = 1)
	{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		Test.AddExpectedError(MakeCaseInsensitiveLiteralPattern(ExpectedString), CompareType, Occurrences);
#else
		Test.AddExpectedErrorPlain(MoveTemp(ExpectedString), CompareType, Occurrences);
#endif
	}
}
