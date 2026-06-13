// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "TextMeasurementInternal.h"

#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignTypeMapper.h"
#include "FontResolver.h"
#include "SharedCoreSemantics.h"
#include "TextMeasurementRuntimeProbe.h"
#include "TextStyleUtils.h"

#include "Design2UMGLog.h"
#include "Styling/SlateTypes.h"

namespace
{
	float ResolveTextWidthHint(const TSharedPtr<FDesignNode>& Node, float ParentContentWidth)
	{
		return Design2UMGCore::SharedSemantics::ResolveWidthHint(Node, ParentContentWidth);
	}

	bool HasPositiveSourceWidth(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Node->Transform.bSourceHasWidth
			&& (Node->Transform.Width.IsFixed() || Node->Transform.Width.IsFit())
			&& FMath::IsFinite(Node->Transform.Width.Value)
			&& Node->Transform.Width.Value > 0.0f;
	}

	bool HasPositiveSourceHeight(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Node->Transform.bSourceHasHeight
			&& (Node->Transform.Height.IsFixed() || Node->Transform.Height.IsFit())
			&& FMath::IsFinite(Node->Transform.Height.Value)
			&& Node->Transform.Height.Value > 0.0f;
	}
}

namespace LineBox
{
namespace
{
	// Single-line text height: default multiplier from declared font size to estimated line box height.
	constexpr float DefaultLineHeightMultiplier = 1.285714f;
}

float EstimateSingleLineTextHeight(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid() || Node->Text.FontSize <= 0.0f)
	{
		return 0.0f;
	}

	const float BaseFontSize = Node->Text.FontSize;
	const float LineHeightMultiplier = Node->Text.LineHeight > 0.0f ? Node->Text.LineHeight : DefaultLineHeightMultiplier;
	return FMath::Max(BaseFontSize, BaseFontSize * LineHeightMultiplier);
}
}

namespace FallbackCalibration
{
namespace
{
	// Historical unresolved-font ceiling: fallback text should not grow past 3 / 4 of the
	// declared size unless a tighter measured scale proves it.
	constexpr float DefaultFontFallbackScale = 0.75f;
	// Measured fallback should not collapse below half of the declared size.
	constexpr float MinMeasuredFontFallbackScale = 0.5f;
	constexpr float MaxMeasuredFontFallbackScale = DefaultFontFallbackScale;
	constexpr int32 MaxProtectedCandidateMeasurements = 6;

	bool IsFinitePositive(const float Value)
	{
		return FMath::IsFinite(Value) && Value > 0.0f;
	}

	bool TryGetCandidateConstraints(const TSharedPtr<FDesignNode>& Node, float& OutWidth, float& OutHeight)
	{
		OutWidth = 0.0f;
		OutHeight = 0.0f;
		if (!Node.IsValid())
		{
			return false;
		}

		OutWidth = ResolveTextWidthHint(Node, 0.0f);
		if (Node->Transform.Width.Value != 0.0f && !FMath::IsFinite(Node->Transform.Width.Value))
		{
			return false;
		}
		if (Node->Transform.Height.Value != 0.0f && !FMath::IsFinite(Node->Transform.Height.Value))
		{
			return false;
		}
		if (Node->Transform.Height.Value > 0.0f)
		{
			OutHeight = Node->Transform.Height.Value;
		}

		return OutWidth > 0.0f || OutHeight > 0.0f;
	}

	bool TryMeasureCandidateDesiredSize(
		const TSharedPtr<FDesignNode>& Node,
		const float Scale,
		const float WidthConstraint,
		FVector2D& OutDesiredSize)
	{
		OutDesiredSize = FVector2D::ZeroVector;
		if (!Node.IsValid() || !IsFinitePositive(Scale) || Node->Text.FontSize <= 0.0f)
		{
			return false;
		}

		const float CandidateFontSize = Node->Text.FontSize * Scale;
		OutDesiredSize = RuntimeProbe::MeasureUMGTextBlockDesiredSize(
			Node,
			CandidateFontSize,
			Node->Text.Content,
			WidthConstraint,
			TextMeasurement::ShouldAutoWrapText(Node));
		return IsFinitePositive(OutDesiredSize.X) && IsFinitePositive(OutDesiredSize.Y);
	}

	bool TryComputeOverflowRatio(
		const FVector2D& DesiredSize,
		const float WidthConstraint,
		const float HeightConstraint,
		float& OutRatio)
	{
		if (!IsFinitePositive(DesiredSize.X) || !IsFinitePositive(DesiredSize.Y))
		{
			return false;
		}

		OutRatio = 1.0f;
		if (WidthConstraint > 0.0f)
		{
			if (!FMath::IsFinite(WidthConstraint))
			{
				return false;
			}
			if (DesiredSize.X > WidthConstraint + 0.5f)
			{
				OutRatio = FMath::Min(OutRatio, WidthConstraint / DesiredSize.X);
			}
		}
		if (HeightConstraint > 0.0f)
		{
			if (!FMath::IsFinite(HeightConstraint))
			{
				return false;
			}
			if (DesiredSize.Y > HeightConstraint + 0.5f)
			{
				OutRatio = FMath::Min(OutRatio, HeightConstraint / DesiredSize.Y);
			}
		}

		return FMath::IsFinite(OutRatio) && OutRatio > 0.0f;
	}

	enum class EPhase32B1LayoutShapeGoal : uint8
	{
		Unknown,
		FixedWidthSingleLine,
		FixedWidthMultilineBody,
		IntrinsicSingleLineUiLabel,
		TruncateEllipsis,
		IconFontText,
	};

	struct FPhase32B1LayoutShapeDecision
	{
		EPhase32B1LayoutShapeGoal Goal = EPhase32B1LayoutShapeGoal::Unknown;
		float WidthConstraint = 0.0f;
		float HeightConstraint = 0.0f;
		bool bUseLineBoxGoal = false;
		bool bUseWidthGoal = false;
	};

	FPhase32B1LayoutShapeDecision ResolvePhase32B1LayoutShapeGoal(const TSharedPtr<FDesignNode>& Node)
	{
		FPhase32B1LayoutShapeDecision Decision;
		if (!Node.IsValid())
		{
			return Decision;
		}

		Decision.WidthConstraint = ResolveTextWidthHint(Node, 0.0f);
		Decision.HeightConstraint = IsFinitePositive(Node->Transform.Height.Value)
			? Node->Transform.Height.Value
			: 0.0f;

		const bool bHasWidth = IsFinitePositive(Decision.WidthConstraint);
		const bool bHasHeight = IsFinitePositive(Decision.HeightConstraint);
		const bool bFixedWidth =
			Node->Text.TextGrowth == EDesignTextGrowth::FixedWidth
			|| Node->Text.TextGrowth == EDesignTextGrowth::FixedWidthHeight;

		if (Node->Type == EDesignNodeType::IconFont
			|| !Node->Text.IconFontName.IsEmpty()
			|| !Node->Text.IconFontFamily.IsEmpty())
		{
			Decision.Goal = EPhase32B1LayoutShapeGoal::IconFontText;
			Decision.bUseLineBoxGoal = true;
			Decision.bUseWidthGoal = bHasWidth;
			return Decision;
		}

		if (bHasWidth && SourceMetadata::IsFigmaTruncateTextNode(Node))
		{
			Decision.Goal = EPhase32B1LayoutShapeGoal::TruncateEllipsis;
			Decision.bUseLineBoxGoal = true;
			Decision.bUseWidthGoal = true;
			return Decision;
		}

		if (bHasWidth && bHasHeight && bFixedWidth)
		{
			Decision.Goal = EPhase32B1LayoutShapeGoal::FixedWidthMultilineBody;
			Decision.bUseLineBoxGoal = true;
			Decision.bUseWidthGoal = true;
			return Decision;
		}

		if (bHasWidth && !bHasHeight && Node->Text.TextGrowth == EDesignTextGrowth::FixedWidth)
		{
			Decision.Goal = EPhase32B1LayoutShapeGoal::FixedWidthSingleLine;
			Decision.bUseLineBoxGoal = true;
			Decision.bUseWidthGoal = true;
			return Decision;
		}

		if (!bHasWidth && !bHasHeight && Node->Text.TextGrowth == EDesignTextGrowth::Auto)
		{
			Decision.Goal = EPhase32B1LayoutShapeGoal::IntrinsicSingleLineUiLabel;
			Decision.bUseLineBoxGoal = true;
			return Decision;
		}

		return Decision;
	}

	TextMeasurementRuntimeProbe::FTextMeasureRequest BuildMetricProbeRequest(
		const TSharedPtr<FDesignNode>& Node,
		const FontCalibration::FLocalFontIdentityPayload& Payload)
	{
		TextMeasurementRuntimeProbe::FTextMeasureRequest Request;
		if (!Node.IsValid())
		{
			return Request;
		}

		Request.Text = TEXT("Hg");
		Request.FontSize = Node->Text.FontSize;
		Request.FontAssetPath = FontCalibration::ResolveMetricFontAssetPath(Payload);
		Request.TypefaceName = Payload.ResolvedTypeface;
		Request.bRequiresResolvedFontAsset = true;
		return Request;
	}
}

extern const float IntrinsicSingleLineSlackThreshold = 1.08f;

bool TryGetDefaultSlateTextFontAssetPath(FString& OutAssetPath)
{
	const UObject* DefaultFontObject = FTextBlockStyle::GetDefault().Font.FontObject;
	if (!DefaultFontObject)
	{
		return false;
	}

	OutAssetPath = DefaultFontObject->GetPathName();
	return !OutAssetPath.IsEmpty();
}

bool IsDefaultSlateTextFontAssetPath(const FString& AssetPath)
{
	FString DefaultFontAssetPath;
	return !AssetPath.IsEmpty()
		&& TryGetDefaultSlateTextFontAssetPath(DefaultFontAssetPath)
		&& AssetPath.Equals(DefaultFontAssetPath, ESearchCase::IgnoreCase);
}

FString NormalizeTextMeasurementFontFamilyKey(const FString& FontFamily)
{
	FString Normalized = FontFamily.ToLower();
	Normalized.ReplaceInline(TEXT(" "), TEXT(""));
	Normalized.ReplaceInline(TEXT("-"), TEXT(""));
	Normalized.ReplaceInline(TEXT("_"), TEXT(""));
	return Normalized;
}

float FindFamilySpecificTextFallbackScale(const FString& NormalizedFamily)
{
	// Font-family-specific fallback scale table. Fonts with measurably different glyph metrics
	// get a tighter scale to keep compiled text within the declared bounding box.
	static const TPair<const TCHAR*, float> FamilyFallbackScales[] = {
		{ TEXT("geist"), 0.71f },
		{ TEXT("robotomono"), 0.73f },
	};

	for (const TPair<const TCHAR*, float>& Entry : FamilyFallbackScales)
	{
		if (NormalizedFamily == Entry.Key)
		{
			return Entry.Value;
		}
	}

	return DefaultFontFallbackScale;
}

float FindFamilySpecificTextFallbackScale()
{
	return FindFamilySpecificTextFallbackScale(FString());
}

bool ShouldUseDeclaredFontSizeDirectly(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid() || Node->Text.FontSize <= 0.0f)
	{
		return false;
	}

	const FString AssetPath = FFontResolver::FindFontAssetPath(Node->Text.FontFamily);
	return IsDefaultSlateTextFontAssetPath(AssetPath);
}

float ResolveBaselineTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (const TOptional<float> SemanticScale = TryResolveSemanticSingleLineFallbackScale(Node))
	{
		return SemanticScale.GetValue();
	}

	float FallbackScale = DefaultFontFallbackScale;
	if (const TOptional<float> BaselineScale = TryResolveBaselineTextFontFallbackScale(Node))
	{
		FallbackScale = FMath::Min(FallbackScale, BaselineScale.GetValue());
	}

	return FallbackScale;
}

float ResolveLineBoxFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (const TOptional<float> SemanticScale = TryResolveSemanticSingleLineFallbackScale(Node))
	{
		return SemanticScale.GetValue();
	}

	float FallbackScale = DefaultFontFallbackScale;
	if (const TOptional<float> BaselineScale = TryResolveBaselineTextFontFallbackScale(Node))
	{
		FallbackScale = FMath::Min(FallbackScale, BaselineScale.GetValue());
	}

	return FallbackScale;
}

TOptional<float> TryResolveSemanticSingleLineFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->Text.FontSize <= 0.0f
		|| Node->Transform.bSourceHasWidth
		|| Node->Transform.Width.IsFill()
		|| Node->Text.Content.Contains(TEXT("\n"))
		|| TextMeasurement::IsWrappingTextMode(Node))
	{
		return {};
	}

	if (SingleLineHeuristics::IsExpandedRefIntrinsicSingleLineText(Node))
	{
		// Expanded component text splits into two stable runtime shapes: proportional
		// content rows need the tighter line-box scale, while mono/status chips keep the
		// historical default ceiling. The split is glyph-metric based, not font-name based.
		return SingleLineHeuristics::ShouldUseTightExpandedRefIntrinsicScale(Node)
			? 0.71f
			: DefaultFontFallbackScale;
	}

	// Dense all-caps metric tokens behave like compact UI identifiers rather than prose.
	// Keep them below the broad 0.75 ceiling without routing on a specific font family.
	if (SingleLineHeuristics::IsDenseTechnicalTokenText(Node))
	{
		return 0.73f;
	}

	return {};
}

TOptional<float> TryResolveBaselineTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->Text.FontSize <= 0.0f
		|| !TextMeasurementRuntimeProbe::IsAvailable())
	{
		return {};
	}

	const FString ProbeKey = CacheTrace::BuildBaselineFallbackScaleKey(Node);
	float CachedScale = 0.0f;
	if (!ProbeKey.IsEmpty() && CacheTrace::TryGetBaselineFallbackScale(ProbeKey, CachedScale))
	{
		return CachedScale;
	}

	const float TargetLineBoxHeight = LineBox::EstimateSingleLineTextHeight(Node);
	if (TargetLineBoxHeight <= KINDA_SMALL_NUMBER)
	{
		return {};
	}

	const float MeasuredLineHeight = RuntimeProbe::MeasureSingleLineTextBlockHeight(Node, Node->Text.FontSize);
	if (MeasuredLineHeight <= KINDA_SMALL_NUMBER)
	{
		return {};
	}

	const float BaselineScale = FMath::Clamp(
		TargetLineBoxHeight / MeasuredLineHeight,
		MinMeasuredFontFallbackScale,
		MaxMeasuredFontFallbackScale);
	if (!ProbeKey.IsEmpty())
	{
		CacheTrace::StoreBaselineFallbackScale(ProbeKey, BaselineScale);
	}

	return BaselineScale;
}

TOptional<float> TryResolveMeasuredTextFontFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->Text.FontSize <= 0.0f
		|| !TextMeasurementRuntimeProbe::IsAvailable())
	{
		return {};
	}

	float BestScale = 1.0f;
	bool bHasScale = false;
	const FPhase32B1LayoutShapeDecision LayoutDecision = ResolvePhase32B1LayoutShapeGoal(Node);
	if (LayoutDecision.Goal == EPhase32B1LayoutShapeGoal::Unknown)
	{
		return {};
	}

	if (LayoutDecision.bUseLineBoxGoal)
	{
		const float TargetLineBoxHeight = LineBox::EstimateSingleLineTextHeight(Node);
		if (TargetLineBoxHeight > 0.0f)
		{
			const float MeasuredLineHeight = RuntimeProbe::MeasureSingleLineTextBlockHeight(Node, Node->Text.FontSize);
			if (MeasuredLineHeight > KINDA_SMALL_NUMBER)
			{
				BestScale = TargetLineBoxHeight / MeasuredLineHeight;
				bHasScale = true;
			}
		}
	}

	if (LayoutDecision.Goal == EPhase32B1LayoutShapeGoal::FixedWidthMultilineBody
		&& HasPositiveSourceWidth(Node)
		&& HasPositiveSourceHeight(Node)
		&& LayoutDecision.WidthConstraint > 0.0f
		&& LayoutDecision.HeightConstraint > 0.0f)
	{
		const FVector2D DesiredSize = RuntimeProbe::MeasureUMGTextBlockDesiredSize(
			Node,
			Node->Text.FontSize,
			Node->Text.Content,
			LayoutDecision.WidthConstraint,
			TextMeasurement::ShouldAutoWrapText(Node));
		if (DesiredSize.X > KINDA_SMALL_NUMBER && DesiredSize.Y > KINDA_SMALL_NUMBER)
		{
			const float WrappedScale = FMath::Min(
				LayoutDecision.WidthConstraint / DesiredSize.X,
				LayoutDecision.HeightConstraint / DesiredSize.Y);
			if (FMath::IsFinite(WrappedScale) && WrappedScale > 0.0f)
			{
				BestScale = bHasScale ? FMath::Min(BestScale, WrappedScale) : WrappedScale;
				bHasScale = true;
			}
		}
	}

	if (LayoutDecision.bUseWidthGoal && !Node->Text.Content.IsEmpty() && LayoutDecision.WidthConstraint > 0.0f)
	{
		const int32 LetterSpacingTracking = Node->Text.LetterSpacing != 0.0f
			? TextStyleUtils::ConvertLetterSpacingPxToTracking(Node->Text.LetterSpacing, Node->Text.FontSize, Node->Text.FontSize)
			: 0;
		const FVector2D UnwrappedSize = RuntimeProbe::MeasureTextSize(
			Node->Text.Content,
			Node->Text.FontSize,
			Node->Text.FontFamily,
			Node->Text.FontWeight,
			LetterSpacingTracking);
		if (UnwrappedSize.X > KINDA_SMALL_NUMBER)
		{
			const float WidthScale = LayoutDecision.WidthConstraint / UnwrappedSize.X;
			BestScale = bHasScale ? FMath::Min(BestScale, WidthScale) : WidthScale;
			bHasScale = true;
		}
	}

	if (!bHasScale)
	{
		return {};
	}

	return FMath::Clamp(BestScale, MinMeasuredFontFallbackScale, MaxMeasuredFontFallbackScale);
}

FProtectedMeasuredCandidate ResolveProtectedMeasuredCandidate(const TSharedPtr<FDesignNode>& Node, const float StableAcceptedScale)
{
	FProtectedMeasuredCandidate Result;
	Result.StableAcceptedScale = StableAcceptedScale;
	if (!Node.IsValid()
		|| Node->Text.FontSize <= 0.0f
		|| !TextMeasurementRuntimeProbe::IsAvailable())
	{
		return Result;
	}

	FontCalibration::FLocalFontIdentityPayload Payload;
	if (!FontCalibration::BuildLocalFontIdentityPayload(Node, Payload))
	{
		Result.bBlockedByMissingFontIdentity = true;
		return Result;
	}
	Result.FallbackState = Payload.FallbackState;
	if (!Payload.bAllowsMeasuredCandidate)
	{
		Result.bBlockedByFallbackState = true;
		return Result;
	}

	const TextMeasurementRuntimeProbe::FSlateTextMetricFingerprint Metric =
		TextMeasurementRuntimeProbe::MeasureSlateTextMetricFingerprint(BuildMetricProbeRequest(Node, Payload));
	Result.MetricToken = TextMeasurementRuntimeProbe::SerializeSlateTextMetricToken(Metric);
	if (!Metric.bIsAvailable)
	{
		return Result;
	}

	float WidthConstraint = 0.0f;
	float HeightConstraint = 0.0f;
	if (!TryGetCandidateConstraints(Node, WidthConstraint, HeightConstraint))
	{
		return Result;
	}

	float CandidateScale = DefaultFontFallbackScale;
	for (int32 Attempt = 0; Attempt < MaxProtectedCandidateMeasurements; ++Attempt)
	{
		FVector2D DesiredSize = FVector2D::ZeroVector;
		if (!TryMeasureCandidateDesiredSize(Node, CandidateScale, WidthConstraint, DesiredSize))
		{
			Result.MeasurementAttempts = Attempt + 1;
			return Result;
		}

		Result.MeasurementAttempts = Attempt + 1;
		float OverflowRatio = 1.0f;
		if (!TryComputeOverflowRatio(DesiredSize, WidthConstraint, HeightConstraint, OverflowRatio))
		{
			return Result;
		}

		if (OverflowRatio >= 1.0f)
		{
			Result.CandidateScale = CandidateScale;
			Result.bCandidateAvailable = true;
			return Result;
		}

		const float NextScale = FMath::Clamp(
			CandidateScale * OverflowRatio,
			MinMeasuredFontFallbackScale,
			MaxMeasuredFontFallbackScale);
		if (!FMath::IsFinite(NextScale) || NextScale >= CandidateScale - KINDA_SMALL_NUMBER)
		{
			return Result;
		}
		CandidateScale = NextScale;
	}

	return Result;
}

float ResolveTextFontSizeFallback(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid())
	{
		return 0.0f;
	}

	float FallbackScale = FindFamilySpecificTextFallbackScale(
		NormalizeTextMeasurementFontFamilyKey(Node->Text.FontFamily));
	if (SingleLineHeuristics::ShouldUseMeasuredTextFontFallbackScale(Node)
		&& !SingleLineHeuristics::ShouldPreferIntrinsicSingleLineFallbackFontSize(Node))
	{
		if (const TOptional<float> MeasuredScale = TryResolveMeasuredTextFontFallbackScale(Node))
		{
			FallbackScale = FMath::Min(FallbackScale, MeasuredScale.GetValue());
		}
	}

	return Node->Text.FontSize * FallbackScale;
}
}

namespace FontCalibration
{
namespace
{
	bool HasDeclaredFontFields(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& (!Node->Text.FontFamily.TrimStartAndEnd().IsEmpty()
				|| !Node->Text.FontPostScriptName.TrimStartAndEnd().IsEmpty());
	}

	bool HasIdentityTarget(const FFontResolutionDiagnostic& Diagnostic)
	{
		return !Diagnostic.ResolvedAssetPath.IsEmpty()
			|| !Diagnostic.FallbackAssetPath.IsEmpty()
			|| !Diagnostic.FallbackToken.IsEmpty()
			|| !Diagnostic.UnresolvedToken.IsEmpty();
	}

	bool IsAllowedMeasuredFallbackState(const FString& FallbackState)
	{
		return FallbackState.Equals(TEXT("exact"), ESearchCase::IgnoreCase)
			|| FallbackState.Equals(TEXT("typeface-substituted"), ESearchCase::IgnoreCase)
			|| FallbackState.Equals(TEXT("weight-substituted"), ESearchCase::IgnoreCase);
	}
}

FString ResolveTypefaceName(const FString& FontFamily, const FString& FontWeight)
{
	FString Typeface = FFontResolver::FindBestTypeface(FontFamily, FontWeight);
	if (Typeface.IsEmpty() && !FontWeight.IsEmpty())
	{
		Typeface = DesignTypeMapper::MapFontWeight(FontWeight).ToString();
	}
	return Typeface;
}

FString ResolveTypefaceName(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid())
	{
		return FString();
	}

	return ResolveTypefaceName(Node->Text.FontFamily, Node->Text.FontWeight);
}

bool BuildLocalFontIdentityPayload(const TSharedPtr<FDesignNode>& Node, FLocalFontIdentityPayload& OutPayload)
{
	OutPayload = FLocalFontIdentityPayload();
	if (!Node.IsValid() || !HasDeclaredFontFields(Node))
	{
		return false;
	}

	const FFontResolutionDiagnostic Diagnostic = FFontResolver::ResolveFontDiagnostic(
		Node->Text.FontFamily,
		Node->Text.FontPostScriptName,
		Node->Text.FontWeight,
		Node->Text.FontStyle,
		false);
	OutPayload.DeclaredFamily = Diagnostic.DeclaredFamily;
	OutPayload.DeclaredPostScriptName = Diagnostic.DeclaredPostScriptName;
	OutPayload.DeclaredWeight = Diagnostic.DeclaredWeight;
	OutPayload.DeclaredStyle = Diagnostic.DeclaredStyle;
	OutPayload.ResolvedAssetPath = Diagnostic.ResolvedAssetPath;
	OutPayload.FallbackAssetPath = Diagnostic.FallbackAssetPath;
	OutPayload.FallbackToken = Diagnostic.FallbackToken;
	OutPayload.UnresolvedToken = Diagnostic.UnresolvedToken;
	OutPayload.ResolvedTypeface = Diagnostic.ResolvedTypeface;
	OutPayload.DeclarationSourceKind = Diagnostic.DeclarationSourceKind;
	OutPayload.ResolutionSourceKind = Diagnostic.ResolutionSourceKind;
	OutPayload.FallbackState = Diagnostic.FallbackState;
	OutPayload.MatchReason = Diagnostic.MatchReason;
	OutPayload.IssueCodes = Diagnostic.IssueCodes;
	OutPayload.bHasRequiredIdentity =
		!OutPayload.DeclarationSourceKind.IsEmpty()
		&& !OutPayload.ResolutionSourceKind.IsEmpty()
		&& !OutPayload.FallbackState.IsEmpty()
		&& HasIdentityTarget(Diagnostic);
	OutPayload.bAllowsMeasuredCandidate =
		OutPayload.bHasRequiredIdentity
		&& IsAllowedMeasuredFallbackState(OutPayload.FallbackState)
		&& !OutPayload.ResolvedAssetPath.IsEmpty()
		&& !OutPayload.ResolvedTypeface.IsEmpty();
	return OutPayload.bHasRequiredIdentity;
}

FString ResolveMetricFontAssetPath(const FLocalFontIdentityPayload& Payload)
{
	return !Payload.ResolvedAssetPath.IsEmpty() ? Payload.ResolvedAssetPath : Payload.FallbackAssetPath;
}

FString BuildFontIdentityKeyToken(const FLocalFontIdentityPayload& Payload)
{
	const FString AssetToken = !Payload.ResolvedAssetPath.IsEmpty()
		? FString::Printf(TEXT("resolved=%s"), *Payload.ResolvedAssetPath.ToLower())
		: (!Payload.FallbackAssetPath.IsEmpty()
			? FString::Printf(TEXT("fallbackAsset=%s"), *Payload.FallbackAssetPath.ToLower())
			: (!Payload.FallbackToken.IsEmpty()
				? FString::Printf(TEXT("fallbackToken=%s"), *Payload.FallbackToken)
				: FString::Printf(TEXT("unresolved=%s"), *Payload.UnresolvedToken)));
	return FString::Printf(
		TEXT("decl=%s|ps=%s|weight=%s|style=%s|%s|typeface=%s|declSource=%s|resSource=%s|fallbackState=%s"),
		*Payload.DeclaredFamily.ToLower(),
		*Payload.DeclaredPostScriptName.ToLower(),
		*Payload.DeclaredWeight.ToLower(),
		*Payload.DeclaredStyle.ToLower(),
		*AssetToken,
		*Payload.ResolvedTypeface.ToLower(),
		*Payload.DeclarationSourceKind,
		*Payload.ResolutionSourceKind,
		*Payload.FallbackState);
}
}


namespace SharedThresholdOwners
{
bool ShouldApplyLargeNonWrappingExplicitLineHeight(const TSharedPtr<FDesignNode>& Node)
{
	return Node.IsValid()
		&& Node->Text.LineHeight > 0.0f
		&& !Node->Text.Content.Contains(TEXT("\n"))
		&& Node->Text.FontSize >= SharedThresholds::LargeNonWrappingFontThreshold
		&& !TextMeasurement::IsWrappingTextMode(Node);
}
}

namespace SingleLineWidthFit
{
bool IsCompactSingleLineLabel(const TSharedPtr<FDesignNode>& Node, const float TargetTextWidth)
{
	return Node.IsValid()
		&& !SingleLineHeuristics::IsHeavyFontWeight(Node->Text.FontWeight)
		&& SingleLineHeuristics::CountSingleLineTextWords(Node) <= SharedThresholds::CompactSingleLineLabelWordLimit
		&& TargetTextWidth <= SharedThresholds::CompactSingleLineLabelWidthThreshold;
}

bool TryBuildProbePlan(
	const TSharedPtr<FDesignNode>& Node,
	const float FallbackFontSize,
	FProbePlan& OutPlan)
{
	if (!Node.IsValid())
	{
		return false;
	}

	OutPlan.TargetTextWidth = (!Node->Transform.Width.IsFill()
		&& Node->Transform.Width.Value > 0.0f
		&& !Node->Text.Content.Contains(TEXT("\n")))
		? Node->Transform.Width.Value
		: 0.0f;
	if (OutPlan.TargetTextWidth <= 0.0f || !TextMeasurementRuntimeProbe::IsAvailable())
	{
		return false;
	}

	const bool bShouldApplyWidthInset =
		HasPositiveSourceWidth(Node)
		&& !TextMeasurement::ShouldAutoWrapText(Node)
		&& Node->Text.TextAlignVertical == EDesignTextAlignVertical::Top;
	const float WidthInset = IsCompactSingleLineLabel(Node, OutPlan.TargetTextWidth)
		? SharedThresholds::CompactSingleLineLabelWidthInset
		: SharedThresholds::DefaultFigmaWidthInset;
	OutPlan.WidthFitTarget = bShouldApplyWidthInset
		? FMath::Max(0.0f, OutPlan.TargetTextWidth - WidthInset)
		: OutPlan.TargetTextWidth;
	OutPlan.MaxProbeFontSize = RuntimeProbe::ResolveMaxProbeFontSize(Node->Text.FontSize, FallbackFontSize);
	OutPlan.bConstrainWrappedHeight =
		TextMeasurement::ShouldAutoWrapText(Node)
		&& Node->Transform.Height.Value > 0.0f;
	OutPlan.bAllowClosestOvershoot =
		!Node->Transform.bSourceHasWidth
		&& SingleLineHeuristics::ShouldAllowClosestOvershootSingleLineWidthFit(Node);
	return true;
}

int32 ResolveWrappedHeightConstrainedFontSize(
	const TSharedPtr<FDesignNode>& Node,
	const FProbePlan& Plan,
	TFunctionRef<float(float)> MeasureWidth)
{
	if (!Plan.bConstrainWrappedHeight)
	{
		return INDEX_NONE;
	}

	int32 BestWrappedFitFontSize = INDEX_NONE;
	float BestWrappedFitError = TNumericLimits<float>::Max();
	constexpr float WrappedHeightTolerance = 1.0f;
	for (int32 CandidateFontSize = 1; CandidateFontSize <= Plan.MaxProbeFontSize; ++CandidateFontSize)
	{
		const float CandidateWidth = MeasureWidth(static_cast<float>(CandidateFontSize));
		if (CandidateWidth <= 0.0f)
		{
			continue;
		}

		const FVector2D CandidateWrappedSize = RuntimeProbe::MeasureUMGTextBlockDesiredSize(
			Node,
			static_cast<float>(CandidateFontSize),
			Node->Text.Content,
			Plan.TargetTextWidth,
			true);
		if (CandidateWrappedSize.X <= 0.0f || CandidateWrappedSize.Y <= 0.0f)
		{
			continue;
		}

		if (CandidateWrappedSize.Y > Node->Transform.Height.Value + WrappedHeightTolerance)
		{
			continue;
		}

		const float Error = FMath::Abs(CandidateWidth - Plan.TargetTextWidth);
		if (BestWrappedFitFontSize == INDEX_NONE
			|| Error + KINDA_SMALL_NUMBER < BestWrappedFitError
			|| (FMath::IsNearlyEqual(Error, BestWrappedFitError, 0.01f) && CandidateFontSize > BestWrappedFitFontSize))
		{
			BestWrappedFitFontSize = CandidateFontSize;
			BestWrappedFitError = Error;
		}
	}

	return BestWrappedFitFontSize;
}
}


#if WITH_AUTOMATION_WORKER
float TextMeasurement::Testing::EstimateSingleLineHeight(const TSharedPtr<FDesignNode>& Node)
{
	return LineBox::EstimateSingleLineTextHeight(Node);
}

float TextMeasurement::Testing::ResolveBaselineFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	return FallbackCalibration::ResolveBaselineTextFontFallbackScale(Node);
}

float TextMeasurement::Testing::ResolveLineBoxFallbackScale(const TSharedPtr<FDesignNode>& Node)
{
	return FallbackCalibration::ResolveLineBoxFallbackScale(Node);
}

float TextMeasurement::Testing::ResolveSingleLineWidthFitTarget(const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid()
		|| Node->Transform.Width.IsFill()
		|| Node->Transform.Width.Value <= 0.0f
		|| Node->Text.Content.Contains(TEXT("\n")))
	{
		return 0.0f;
	}

	const bool bShouldApplySourceWidthInset =
		HasPositiveSourceWidth(Node)
		&& !TextMeasurement::ShouldAutoWrapText(Node)
		&& Node->Text.TextAlignVertical == EDesignTextAlignVertical::Top;
	const float WidthInset = bShouldApplySourceWidthInset
		? (SingleLineWidthFit::IsCompactSingleLineLabel(Node, Node->Transform.Width.Value)
			? SharedThresholds::CompactSingleLineLabelWidthInset
			: SharedThresholds::DefaultFigmaWidthInset)
		: 0.0f;
	return bShouldApplySourceWidthInset
		? FMath::Max(0.0f, Node->Transform.Width.Value - WidthInset)
		: Node->Transform.Width.Value;
}

int32 TextMeasurement::Testing::ResolveBestWidthFitFontSizeFromWidths(
	float FallbackFontSize,
	int32 MaxProbeFontSize,
	float TargetWidth,
	bool bAllowClosestOvershoot,
	const TArray<float>& CandidateWidths)
{
	return RuntimeProbeSizing::ResolveBestWidthFitFontSize(
		FallbackFontSize,
		MaxProbeFontSize,
		TargetWidth,
		bAllowClosestOvershoot,
		[&CandidateWidths](float CandidateFontSize) -> float
		{
			const int32 CandidateIndex = FMath::RoundToInt(CandidateFontSize);
			return CandidateWidths.IsValidIndex(CandidateIndex) ? CandidateWidths[CandidateIndex] : 0.0f;
		});
}

FVector2D TextMeasurement::Testing::MeasureFallbackTextSize(const FString& Text, float FontSize)
{
	if (FontSize <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	const FString RuntimeText = TextMeasurement::PrepareRuntimeTextContent(Text);
	return FVector2D(
		RuntimeText.Len() * FontSize * RuntimeProbe::FallbackMeasureWidthScale,
		FontSize * RuntimeProbe::FallbackMeasureHeightScale);
}

float TextMeasurement::Testing::EstimateSingleLineHeuristicWidth(const FString& Text, float FontSize)
{
	if (FontSize <= 0.0f)
	{
		return 0.0f;
	}

	return TextMeasurement::PrepareRuntimeTextContent(Text).Len() * FontSize * SingleLineHeuristics::WidthHeuristicScale;
}

TextMeasurement::Testing::FSlateMetricProbeResult TextMeasurement::Testing::MeasureSlateMetricProbe(
	const TSharedPtr<FDesignNode>& Node,
	const int32 BatchCount)
{
	FSlateMetricProbeResult Result;
	if (!Node.IsValid() || BatchCount <= 0)
	{
		Result.Token = TEXT("metric:unavailable");
		return Result;
	}

	FontCalibration::FLocalFontIdentityPayload Payload;
	FontCalibration::BuildLocalFontIdentityPayload(Node, Payload);
	const TextMeasurementRuntimeProbe::FTextMeasureRequest Request = FallbackCalibration::BuildMetricProbeRequest(Node, Payload);
	const double StartSeconds = FPlatformTime::Seconds();
	for (int32 Index = 0; Index < BatchCount; ++Index)
	{
		const TextMeasurementRuntimeProbe::FSlateTextMetricFingerprint Metric =
			TextMeasurementRuntimeProbe::MeasureSlateTextMetricFingerprint(Request);
		Result.SignedBaselineRatio = Metric.SignedBaselineRatio;
		Result.MaxHeightRatio = Metric.MaxHeightRatio;
		Result.bAvailable = Metric.bIsAvailable;
		Result.Token = TextMeasurementRuntimeProbe::SerializeSlateTextMetricToken(Metric);
	}
	Result.ElapsedSeconds = FPlatformTime::Seconds() - StartSeconds;
	if (Result.Token.IsEmpty())
	{
		Result.Token = TEXT("metric:unavailable");
	}
	return Result;
}

FString TextMeasurement::Testing::BuildSlateMetricToken(
	const float SignedBaselineRatio,
	const float MaxHeightRatio,
	const bool bAvailable)
{
	TextMeasurementRuntimeProbe::FSlateTextMetricFingerprint Metric;
	Metric.SignedBaselineRatio = SignedBaselineRatio;
	Metric.MaxHeightRatio = MaxHeightRatio;
	Metric.bIsAvailable = bAvailable;
	return TextMeasurementRuntimeProbe::SerializeSlateTextMetricToken(Metric);
}

TextMeasurement::Testing::FProtectedMeasuredCandidateResult TextMeasurement::Testing::ResolveProtectedMeasuredCandidate(
	const TSharedPtr<FDesignNode>& Node)
{
	const float StableFontSize = FallbackCalibration::ResolveTextFontSizeFallback(Node);
	const float StableScale = Node.IsValid() && Node->Text.FontSize > 0.0f
		? StableFontSize / Node->Text.FontSize
		: 0.0f;
	const FallbackCalibration::FProtectedMeasuredCandidate Candidate =
		FallbackCalibration::ResolveProtectedMeasuredCandidate(Node, StableScale);

	FProtectedMeasuredCandidateResult Result;
	Result.StableAcceptedScale = Candidate.StableAcceptedScale;
	Result.CandidateScale = Candidate.CandidateScale;
	Result.MeasurementAttempts = Candidate.MeasurementAttempts;
	Result.bCandidateAvailable = Candidate.bCandidateAvailable;
	Result.bBlockedByMissingFontIdentity = Candidate.bBlockedByMissingFontIdentity;
	Result.bBlockedByFallbackState = Candidate.bBlockedByFallbackState;
	Result.FallbackState = Candidate.FallbackState;
	Result.MetricToken = Candidate.MetricToken;
	return Result;
}
#endif
