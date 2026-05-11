// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Design2UMGLog.h"
#include "DesignSchema.h"
#include "FontResolver.h"
#include "GenericPlatform/GenericPlatformOutputDevices.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Paths.h"
#include "TextMeasurement.h"

#if WITH_AUTOMATION_WORKER
// Tests-only bridge for text-resolution evidence. Keep this out of Core public headers.
struct FTextResolutionTrace
{
	FString NodeName;
	FString ResolutionPath;
	float SourceFontSize = 0.0f;
	float FallbackScale = 0.0f;
	float FallbackFontSize = 0.0f;
	float CompiledFontSize = 0.0f;
	FVector2D CompiledBounds = FVector2D::ZeroVector;
	FString CalibrationKey;
	FString FontFamily;
	FString FontWeight;
	float NodeWidth = 0.0f;
	float NodeHeight = 0.0f;
	bool bSourceHasWidth = false;
	bool bAutoWrap = false;
};

namespace TextMeasurementTestTrace
{
	namespace Private
	{
		class FTextDecisionCapture final : public FOutputDevice
		{
		public:
			virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
			{
				if (V != nullptr)
				{
					const FString Message(V);
					if (Message.Contains(TEXT("TextDecision:")))
					{
						CapturedDecision = Message;
					}
				}
			}

			virtual bool CanBeUsedOnAnyThread() const override
			{
				return true;
			}

			virtual bool CanBeUsedOnPanicThread() const override
			{
				return true;
			}

			const FString& GetCapturedDecision() const
			{
				return CapturedDecision;
			}

		private:
			FString CapturedDecision;
		};

		inline bool ExtractTokenValue(const FString& Message, const TCHAR* Token, FString& OutValue)
		{
			const FString TokenString(Token);
			const int32 TokenStart = Message.Find(TokenString, ESearchCase::CaseSensitive);
			if (TokenStart == INDEX_NONE)
			{
				return false;
			}

			const int32 ValueStart = TokenStart + TokenString.Len();
			int32 ValueEnd = Message.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			if (ValueEnd == INDEX_NONE)
			{
				ValueEnd = Message.Len();
			}

			OutValue = Message.Mid(ValueStart, ValueEnd - ValueStart);
			return !OutValue.IsEmpty();
		}

		inline bool IsFigmaTruncateTextNode(const TSharedPtr<FDesignNode>& Node)
		{
			if (!Node.IsValid())
			{
				return false;
			}

			const FString* Value = Node->Metadata.Find(TEXT("source.text.truncate"));
			return Value && Value->Equals(TEXT("true"), ESearchCase::IgnoreCase);
		}

		inline FString NormalizeFontFamilyKey(const FString& FontFamily)
		{
			FString Normalized = FontFamily.ToLower();
			Normalized.ReplaceInline(TEXT(" "), TEXT(""));
			Normalized.ReplaceInline(TEXT("-"), TEXT(""));
			Normalized.ReplaceInline(TEXT("_"), TEXT(""));
			return Normalized;
		}

		inline FString ResolveTypefaceName(const TSharedPtr<FDesignNode>& Node)
		{
			if (!Node.IsValid())
			{
				return FString();
			}

			return FFontResolver::FindBestTypeface(Node->Text.FontFamily, Node->Text.FontWeight);
		}

		inline FString BuildCalibrationKey(const TSharedPtr<FDesignNode>& Node)
		{
			if (!Node.IsValid())
			{
				return FString();
			}

			const FString LineHeightKey = Node->Text.LineHeight > 0.0f
				? FString::Printf(TEXT("%.4f"), Node->Text.LineHeight)
				: TEXT("default");
			const FString LetterSpacingKey = FString::Printf(TEXT("%.4f"), Node->Text.LetterSpacing);
			const FString WidthKey = FString::Printf(
				TEXT("%d:%.2f"),
				static_cast<int32>(Node->Transform.Width.Mode),
				Node->Transform.Width.Value);
			const FString HeightKey = FString::Printf(
				TEXT("%d:%.2f"),
				static_cast<int32>(Node->Transform.Height.Mode),
				Node->Transform.Height.Value);
			return FString::Printf(
				TEXT("%s|%s|%s|%.1f|ls=%s|w=%s|h=%s|grow=%d|align=%d|valign=%d|wrap=%d|truncate=%d"),
				*NormalizeFontFamilyKey(Node->Text.FontFamily),
				*ResolveTypefaceName(Node),
				*LineHeightKey,
				Node->Text.FontSize,
				*LetterSpacingKey,
				*WidthKey,
				*HeightKey,
				static_cast<int32>(Node->Text.TextGrowth),
				static_cast<int32>(Node->Text.TextAlign),
				static_cast<int32>(Node->Text.TextAlignVertical),
				TextMeasurement::ShouldAutoWrapText(Node) ? 1 : 0,
				IsFigmaTruncateTextNode(Node) ? 1 : 0);
		}

		inline void FillResolutionEvidenceFromLog(const FString& DecisionLog, FTextResolutionTrace& Trace)
		{
			if (DecisionLog.IsEmpty())
			{
				return;
			}

			FString ResolutionPath;
			if (ExtractTokenValue(DecisionLog, TEXT("reason="), ResolutionPath))
			{
				Trace.ResolutionPath = ResolutionPath;
			}

			FString FallbackFontSize;
			if (ExtractTokenValue(DecisionLog, TEXT("fallback="), FallbackFontSize))
			{
				Trace.FallbackFontSize = FCString::Atof(*FallbackFontSize);
				if (Trace.SourceFontSize > 0.0f)
				{
					Trace.FallbackScale = Trace.FallbackFontSize / Trace.SourceFontSize;
				}
			}
		}

		inline FString FindLatestDecisionLogForNode(const FString& NodeName)
		{
			const FString CurrentLogFilePath = FPaths::ConvertRelativePathToFull(FGenericPlatformOutputDevices::GetAbsoluteLogFilename());
			if (CurrentLogFilePath.IsEmpty())
			{
				return FString();
			}

			FString LogContent;
			if (!FFileHelper::LoadFileToString(LogContent, *CurrentLogFilePath) || LogContent.IsEmpty())
			{
				return FString();
			}

			TArray<FString> Lines;
			LogContent.ParseIntoArrayLines(Lines, false);
			const FString NodeToken = NodeName.IsEmpty()
				? FString()
				: FString::Printf(TEXT("node='%s'"), *NodeName);

			for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
			{
				const FString& Line = Lines[Index];
				if (!Line.Contains(TEXT("TextDecision:")))
				{
					continue;
				}

				if (NodeToken.IsEmpty() || Line.Contains(NodeToken))
				{
					return Line;
				}
			}

			return FString();
		}

		inline FString InferResolutionPathFallback(const TSharedPtr<FDesignNode>& Node, const FTextResolutionTrace& Trace)
		{
			if (!Node.IsValid())
			{
				return FString();
			}

			if (Trace.SourceFontSize <= 0.0f)
			{
				return TEXT("fallback-no-slate");
			}

			if (Trace.FallbackFontSize > 0.0f
				&& Trace.CompiledFontSize > 0.0f
				&& FMath::IsNearlyEqual(Trace.CompiledFontSize, Trace.FallbackFontSize, 0.01f))
			{
				return TEXT("fallback-no-width-probe");
			}

			return TEXT("trace-log-unavailable");
		}
	}

	inline FTextResolutionTrace DumpTextResolutionTrace(const TSharedPtr<FDesignNode>& Node)
	{
		FTextResolutionTrace Trace;
		if (!Node.IsValid())
		{
			return Trace;
		}

		Trace.NodeName = Node->Name;
		Trace.FontFamily = Node->Text.FontFamily;
		Trace.FontWeight = Node->Text.FontWeight;
		Trace.SourceFontSize = Node->Text.FontSize;
		Trace.NodeWidth = Node->Transform.Width.Value;
		Trace.NodeHeight = Node->Transform.Height.Value;
		Trace.bSourceHasWidth = Node->Transform.bSourceHasWidth;
		Trace.bAutoWrap = TextMeasurement::ShouldAutoWrapText(Node);
		Trace.CalibrationKey = Private::BuildCalibrationKey(Node);

		Private::FTextDecisionCapture Capture;
		if (GLog != nullptr)
		{
			GLog->AddOutputDevice(&Capture);
		}
		Trace.CompiledFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
		if (GLog != nullptr)
		{
			GLog->FlushThreadedLogs();
			GLog->Flush();
			GLog->RemoveOutputDevice(&Capture);
		}

		Private::FillResolutionEvidenceFromLog(Capture.GetCapturedDecision(), Trace);
		if (Trace.ResolutionPath.IsEmpty())
		{
			Private::FillResolutionEvidenceFromLog(Private::FindLatestDecisionLogForNode(Trace.NodeName), Trace);
		}
		if (Trace.ResolutionPath.IsEmpty())
		{
			Trace.ResolutionPath = Private::InferResolutionPathFallback(Node, Trace);
		}
		if (Trace.FallbackFontSize <= 0.0f
			&& Trace.SourceFontSize > 0.0f
			&& Trace.CompiledFontSize > 0.0f
			&& Trace.ResolutionPath.StartsWith(TEXT("fallback")))
		{
			Trace.FallbackFontSize = Trace.CompiledFontSize;
			Trace.FallbackScale = Trace.FallbackFontSize / Trace.SourceFontSize;
		}
		Trace.CompiledBounds = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
		return Trace;
	}
}
#endif
