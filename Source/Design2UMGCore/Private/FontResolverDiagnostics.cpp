// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "FontResolver.h"
#include "FontResolverInternal.h"
#include "Design2UMGFontAccess.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Misc/PackageName.h"

using FontResolverInternal::EExplicitFontMappingResult;
using FontResolverInternal::NormalizeFontLookupKey;
using FontResolverInternal::IsProjectFontAssetPath;
using FontResolverInternal::IsEngineFontAssetPath;
using FontResolverInternal::TryResolveExplicitFontMapping;
using FontResolverInternal::HasExplicitFontMappingKey;

namespace
{
	FString NormalizeFontStyleToken(const FString& Style)
	{
		const FString Normalized = Style.ToLower().TrimStartAndEnd();
		return (Normalized == TEXT("italic") || Normalized == TEXT("oblique")) ? Normalized : TEXT("normal");
	}

	FString BuildFontDiagnosticDeclarationKey(
		const FString& Family,
		const FString& PostScriptName,
		const FString& Weight,
		const FString& Style)
	{
		const FString FamilyKey = Family.TrimStartAndEnd().IsEmpty()
			? TEXT("unknown-family")
			: NormalizeFontLookupKey(Family);
		const FString PostScriptKey = PostScriptName.TrimStartAndEnd().IsEmpty()
			? TEXT("none")
			: NormalizeFontLookupKey(PostScriptName);
		const FString WeightKey = Weight.IsEmpty()
			? TEXT("none")
			: FString::FromInt(FFontResolver::ParseWeightValue(Weight));
		return FString::Printf(
			TEXT("%s|%s|%s|%s"),
			*FamilyKey,
			*PostScriptKey,
			*WeightKey,
			*NormalizeFontStyleToken(Style));
	}

	FString ResolveEngineDefaultFontAssetPath()
	{
		FString DefaultFontPackageName = UWidget::GetDefaultFontName();
		if (DefaultFontPackageName.IsEmpty())
		{
			return FString();
		}

		DefaultFontPackageName = FPackageName::ObjectPathToPackageName(DefaultFontPackageName);
		const FString DefaultFontAssetName = FPackageName::GetLongPackageAssetName(DefaultFontPackageName);
		if (DefaultFontAssetName.IsEmpty())
		{
			return FString();
		}

		return DefaultFontPackageName + TEXT(".") + DefaultFontAssetName;
	}

	int32 CountProjectFontAssetCandidates(const FString& Family)
	{
		const FString NormalizedFamily = NormalizeFontLookupKey(Family);
		if (NormalizedFamily.IsEmpty() || NormalizedFamily == TEXT("unknown-family"))
		{
			return 0;
		}

		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> FontAssets;
		AssetRegistryModule.Get().GetAssetsByClass(Design2UMGFontAccess::GetAssetClassKey(), FontAssets, true);

		TSet<FString> MatchingObjectPaths;
		const FString ImportedName = NormalizeFontLookupKey(FPackageName::GetLongPackageAssetName(FFontResolver::MakeImportedFamilyPackageName(Family)));
		for (const FAssetData& Asset : FontAssets)
		{
			const FString ObjectPath = Asset.ToSoftObjectPath().ToString();
			if (IsProjectFontAssetPath(ObjectPath)
				&& (NormalizeFontLookupKey(Asset.AssetName.ToString()) == NormalizedFamily
					|| NormalizeFontLookupKey(Asset.AssetName.ToString()) == ImportedName))
			{
				MatchingObjectPaths.Add(ObjectPath);
			}
		}
		return MatchingObjectPaths.Num();
	}

	void AddFontDiagnosticIssue(FFontResolutionDiagnostic& Diagnostic, const TCHAR* IssueCode)
	{
		Diagnostic.IssueCodes.AddUnique(FString(IssueCode));
	}

	void SetUnresolvedDiagnostic(
		FFontResolutionDiagnostic& Diagnostic,
		const FString& Token,
		const FString& FallbackState,
		const FString& MatchReason)
	{
		Diagnostic.ResolutionSourceKind = TEXT("unresolved");
		Diagnostic.FallbackState = FallbackState;
		Diagnostic.MatchReason = MatchReason;
		Diagnostic.UnresolvedToken = Token;
	}

	// Stage helpers for FFontResolver::ResolveFontDiagnostic: each one decides
	// a single diagnostic sub-domain and returns its decision; the public
	// function assembles those decisions into FFontResolutionDiagnostic.

	enum class EFontDeclarationResolutionKind : uint8
	{
		UnsupportedInput,
		InvalidExplicitMapping,
		EngineDefaultPath,
		RejectedTypeface,
		ExplicitMappingMatch,
		ProjectAssetMatch,
		AmbiguousProjectAssets,
		UnresolvedDeclaration
	};

	struct FFontDeclarationResolution
	{
		EFontDeclarationResolutionKind Kind = EFontDeclarationResolutionKind::UnresolvedDeclaration;
		FString ResolvedAssetPath;
		FString EngineFallbackAssetPath;
		FString ResolvedTypeface;
		bool bTypefaceSubstituted = false;
		bool bUseMappedAssetDefaultTypeface = false;
		bool bAllowFallbackAsset = true;
	};

	struct FExplicitMappedTypefaceDecision
	{
		bool bRejected = false;
		FString Typeface;
	};

	struct FProjectAssetTypefaceDecision
	{
		FString Typeface;
		bool bSubstituted = false;
	};

	struct FFontFallbackAssetDecision
	{
		FString FallbackAssetPath;
		FString FallbackToken;
		FString ResolutionSourceKind;
		FString FallbackState;
		FString MatchReason;
		const TCHAR* IssueCode = nullptr;
	};

	// Decide whether the resolved asset's default typeface is used as-is:
	// no weight declared and the asset lives under /Game.
	bool UsesMappedAssetDefaultTypeface(const FString& Weight, const FString& ResolvedAssetPath)
	{
		return Weight.IsEmpty() && IsProjectFontAssetPath(ResolvedAssetPath);
	}

	// Decide whether an explicitly mapped project asset carries the exact
	// typeface for the declared weight. An undeclared weight keeps the mapped
	// asset's default typeface and can never be rejected.
	FExplicitMappedTypefaceDecision DetermineExplicitMappedTypeface(
		const FString& ResolvedAssetPath,
		const FString& Weight,
		const bool bUseMappedAssetDefaultTypeface)
	{
		FExplicitMappedTypefaceDecision Decision;
		Decision.Typeface = bUseMappedAssetDefaultTypeface
			? FString()
			: FFontResolver::FindExactTypefaceForAssetPath(ResolvedAssetPath, Weight);
		Decision.bRejected = !bUseMappedAssetDefaultTypeface && Decision.Typeface.IsEmpty();
		return Decision;
	}

	// Decide the best-match typeface inside a same-name project asset and
	// whether it substitutes the declared weight's nominal typeface.
	FProjectAssetTypefaceDecision DetermineProjectAssetTypeface(
		const FString& ResolvedAssetPath,
		const FString& Weight,
		const bool bUseMappedAssetDefaultTypeface)
	{
		FProjectAssetTypefaceDecision Decision;
		Decision.Typeface = bUseMappedAssetDefaultTypeface
			? FString()
			: FFontResolver::FindBestTypefaceForAssetPath(ResolvedAssetPath, Weight);
		Decision.bSubstituted = !bUseMappedAssetDefaultTypeface
			&& Decision.Typeface != FFontResolver::TypefaceNameForWeightValue(FFontResolver::ParseWeightValue(Weight));
		return Decision;
	}

	// Decide how a family with no resolvable asset path failed: multiple
	// same-name project assets are ambiguous, everything else (including an
	// empty family) stays a plain unresolved declaration.
	EFontDeclarationResolutionKind ClassifyUnresolvedFontFamily(const FString& FontFamily)
	{
		if (FontFamily.TrimStartAndEnd().IsEmpty())
		{
			return EFontDeclarationResolutionKind::UnresolvedDeclaration;
		}
		if (CountProjectFontAssetCandidates(FontFamily) > 1)
		{
			return EFontDeclarationResolutionKind::AmbiguousProjectAssets;
		}
		return EFontDeclarationResolutionKind::UnresolvedDeclaration;
	}

	// Decide the primary resolution outcome for one declaration, before any
	// fallback asset is considered.
	FFontDeclarationResolution DetermineFontDeclarationResolution(
		const FString& FontFamily,
		const FString& PostScriptName,
		const FString& Weight,
		const bool bUnsupportedInput)
	{
		FFontDeclarationResolution Resolution;

		if (bUnsupportedInput)
		{
			Resolution.Kind = EFontDeclarationResolutionKind::UnsupportedInput;
			Resolution.bAllowFallbackAsset = false;
			return Resolution;
		}

		FString ExplicitPath;
		const EExplicitFontMappingResult ExplicitMappingResult = TryResolveExplicitFontMapping(FontFamily, PostScriptName, ExplicitPath);
		if (ExplicitMappingResult == EExplicitFontMappingResult::Invalid)
		{
			Resolution.Kind = EFontDeclarationResolutionKind::InvalidExplicitMapping;
			Resolution.bAllowFallbackAsset = false;
			return Resolution;
		}

		const FString ResolvedAssetPath = FFontResolver::FindFontAssetPath(FontFamily, PostScriptName);
		if (ResolvedAssetPath.IsEmpty())
		{
			Resolution.Kind = ClassifyUnresolvedFontFamily(FontFamily);
			Resolution.bAllowFallbackAsset =
				Resolution.Kind != EFontDeclarationResolutionKind::AmbiguousProjectAssets;
			return Resolution;
		}

		Resolution.bUseMappedAssetDefaultTypeface = UsesMappedAssetDefaultTypeface(Weight, ResolvedAssetPath);
		if (IsEngineFontAssetPath(ResolvedAssetPath))
		{
			Resolution.Kind = EFontDeclarationResolutionKind::EngineDefaultPath;
			Resolution.EngineFallbackAssetPath = ResolvedAssetPath;
			Resolution.bAllowFallbackAsset = false;
			return Resolution;
		}

		if (ExplicitMappingResult == EExplicitFontMappingResult::Resolved)
		{
			const FExplicitMappedTypefaceDecision TypefaceDecision =
				DetermineExplicitMappedTypeface(ResolvedAssetPath, Weight, Resolution.bUseMappedAssetDefaultTypeface);
			Resolution.Kind = TypefaceDecision.bRejected
				? EFontDeclarationResolutionKind::RejectedTypeface
				: EFontDeclarationResolutionKind::ExplicitMappingMatch;
			Resolution.ResolvedAssetPath = TypefaceDecision.bRejected ? FString() : ResolvedAssetPath;
			Resolution.ResolvedTypeface = TypefaceDecision.Typeface;
			if (TypefaceDecision.bRejected)
			{
				Resolution.bAllowFallbackAsset = false;
			}
			return Resolution;
		}

		const FProjectAssetTypefaceDecision TypefaceDecision =
			DetermineProjectAssetTypeface(ResolvedAssetPath, Weight, Resolution.bUseMappedAssetDefaultTypeface);
		Resolution.Kind = EFontDeclarationResolutionKind::ProjectAssetMatch;
		Resolution.ResolvedAssetPath = ResolvedAssetPath;
		Resolution.ResolvedTypeface = TypefaceDecision.Typeface;
		Resolution.bTypefaceSubstituted = TypefaceDecision.bSubstituted;
		return Resolution;
	}

	// Decide which fallback asset covers an unresolved declaration: the
	// project default font first, the engine default font for the rest.
	FFontFallbackAssetDecision SelectFallbackFontAsset()
	{
		FFontFallbackAssetDecision Decision;

		FString FallbackPath = FFontResolver::FindDefaultFontAssetPath();
		if (!FallbackPath.IsEmpty())
		{
			Decision.FallbackAssetPath = FallbackPath;
			Decision.ResolutionSourceKind = TEXT("default-font-asset");
			Decision.FallbackState = TEXT("default-font-asset");
			Decision.MatchReason = TEXT("default-font-asset-fallback");
			Decision.IssueCode = TEXT("default-font-asset-used");
			return Decision;
		}

		FallbackPath = ResolveEngineDefaultFontAssetPath();
		if (FallbackPath.IsEmpty())
		{
			Decision.FallbackToken = TEXT("engine-default");
		}
		else
		{
			Decision.FallbackAssetPath = FallbackPath;
		}
		Decision.ResolutionSourceKind = TEXT("engine-default");
		Decision.FallbackState = TEXT("engine-default");
		Decision.MatchReason = TEXT("engine-default-fallback");
		Decision.IssueCode = TEXT("engine-default-used");
		return Decision;
	}

	// Decide the diagnostic severity for a primary resolution kind.
	const TCHAR* FontDiagnosticSeverityForKind(const EFontDeclarationResolutionKind Kind)
	{
		switch (Kind)
		{
		case EFontDeclarationResolutionKind::UnsupportedInput:
		case EFontDeclarationResolutionKind::InvalidExplicitMapping:
		case EFontDeclarationResolutionKind::RejectedTypeface:
			return TEXT("error");
		case EFontDeclarationResolutionKind::ExplicitMappingMatch:
		case EFontDeclarationResolutionKind::ProjectAssetMatch:
			return TEXT("info");
		default:
			return TEXT("warning");
		}
	}

	// Decide the match reason for a resolved explicit mapping, keyed on which
	// side of the declaration carries the settings mapping.
	const TCHAR* ExplicitMappingMatchReason(const FString& PostScriptName)
	{
		return HasExplicitFontMappingKey(PostScriptName)
			? TEXT("explicit-postscript-mapping")
			: TEXT("explicit-family-mapping");
	}
}

bool FFontResolver::HasAmbiguousProjectFontAssets(const FString& FontFamily)
{
	return CountProjectFontAssetCandidates(FontFamily) > 1;
}

FFontResolutionDiagnostic FFontResolver::ResolveFontDiagnostic(
	const FString& FontFamily,
	const FString& PostScriptName,
	const FString& Weight,
	const FString& Style,
	const bool bUnsupportedInput,
	const bool bFontUrlIgnored)
{
	FFontResolutionDiagnostic Diagnostic;
	Diagnostic.DeclarationKey = BuildFontDiagnosticDeclarationKey(FontFamily, PostScriptName, Weight, Style);
	Diagnostic.DeclaredFamily = FontFamily.TrimStartAndEnd().IsEmpty() ? TEXT("unknown-family") : FontFamily;
	Diagnostic.DeclaredPostScriptName = PostScriptName;
	Diagnostic.DeclaredWeight = Weight;
	Diagnostic.DeclaredStyle = Style;
	Diagnostic.ResolutionState = TEXT("default-font");
	Diagnostic.DeclarationSourceKind = TEXT("source-declaration");
	Diagnostic.ResolutionSourceKind = TEXT("unresolved");
	Diagnostic.FallbackState = TEXT("unresolved");
	Diagnostic.MatchReason = TEXT("unresolved-declaration");
	Diagnostic.Severity = TEXT("warning");

	if (bFontUrlIgnored)
	{
		AddFontDiagnosticIssue(Diagnostic, TEXT("font-url-ignored"));
	}

	const FFontDeclarationResolution Resolution = DetermineFontDeclarationResolution(
		FontFamily, PostScriptName, Weight, bUnsupportedInput);
	Diagnostic.ResolvedAssetPath = Resolution.ResolvedAssetPath;
	Diagnostic.ResolvedTypeface = Resolution.ResolvedTypeface;
	Diagnostic.Severity = FontDiagnosticSeverityForKind(Resolution.Kind);

	switch (Resolution.Kind)
	{
	case EFontDeclarationResolutionKind::UnsupportedInput:
		Diagnostic.ResolutionState = TEXT("unsupported-input");
		SetUnresolvedDiagnostic(Diagnostic, TEXT("unsupported-input"), TEXT("unsupported-input"), TEXT("declaration-only"));
		AddFontDiagnosticIssue(Diagnostic, TEXT("unsupported-font-file-input"));
		break;

	case EFontDeclarationResolutionKind::InvalidExplicitMapping:
		Diagnostic.ResolutionState = TEXT("rejected");
		SetUnresolvedDiagnostic(Diagnostic, TEXT("unresolved-declaration"), TEXT("unresolved"), TEXT("unresolved-declaration"));
		AddFontDiagnosticIssue(Diagnostic, TEXT("unresolved-declaration"));
		break;

	case EFontDeclarationResolutionKind::EngineDefaultPath:
		Diagnostic.FallbackAssetPath = Resolution.EngineFallbackAssetPath;
		Diagnostic.ResolutionState = TEXT("default-font");
		Diagnostic.ResolutionSourceKind = TEXT("engine-default");
		Diagnostic.FallbackState = TEXT("engine-default");
		Diagnostic.MatchReason = TEXT("engine-default-fallback");
		AddFontDiagnosticIssue(Diagnostic, TEXT("engine-default-used"));
		break;

	case EFontDeclarationResolutionKind::RejectedTypeface:
		Diagnostic.ResolutionState = TEXT("rejected");
		SetUnresolvedDiagnostic(Diagnostic, TEXT("unresolved-declaration"), TEXT("unresolved"), TEXT("unresolved-declaration"));
		AddFontDiagnosticIssue(Diagnostic, TEXT("rejected-typeface"));
		break;

	case EFontDeclarationResolutionKind::ExplicitMappingMatch:
		Diagnostic.ResolutionState = TEXT("exact");
		Diagnostic.ResolutionSourceKind = TEXT("explicit-project-mapping");
		Diagnostic.FallbackState = TEXT("exact");
		Diagnostic.MatchReason = ExplicitMappingMatchReason(PostScriptName);
		break;

	case EFontDeclarationResolutionKind::ProjectAssetMatch:
		Diagnostic.ResolutionState = TEXT("exact");
		Diagnostic.ResolutionSourceKind = TEXT("project-asset");
		Diagnostic.FallbackState = TEXT("exact");
		Diagnostic.MatchReason = TEXT("project-family-match");
		if (Resolution.bTypefaceSubstituted)
		{
			Diagnostic.FallbackState = TEXT("typeface-substituted");
			AddFontDiagnosticIssue(Diagnostic, TEXT("typeface-substituted"));
		}
		break;

	case EFontDeclarationResolutionKind::AmbiguousProjectAssets:
		Diagnostic.ResolutionState = TEXT("ambiguous");
		Diagnostic.ResolutionSourceKind = TEXT("project-asset");
		Diagnostic.FallbackState = TEXT("family-substituted");
		Diagnostic.MatchReason = TEXT("project-family-match");
		AddFontDiagnosticIssue(Diagnostic, TEXT("ambiguous-font-match"));
		break;

	case EFontDeclarationResolutionKind::UnresolvedDeclaration:
		SetUnresolvedDiagnostic(Diagnostic, TEXT("unresolved-declaration"), TEXT("unresolved"), TEXT("unresolved-declaration"));
		AddFontDiagnosticIssue(Diagnostic, TEXT("unresolved-declaration"));
		break;
	}

	if (Diagnostic.ResolvedAssetPath.IsEmpty() && Resolution.bAllowFallbackAsset)
	{
		const FFontFallbackAssetDecision Fallback = SelectFallbackFontAsset();
		Diagnostic.UnresolvedToken.Empty();
		Diagnostic.FallbackAssetPath = Fallback.FallbackAssetPath;
		Diagnostic.FallbackToken = Fallback.FallbackToken;
		Diagnostic.ResolutionSourceKind = Fallback.ResolutionSourceKind;
		Diagnostic.FallbackState = Fallback.FallbackState;
		Diagnostic.MatchReason = Fallback.MatchReason;
		AddFontDiagnosticIssue(Diagnostic, Fallback.IssueCode);
	}

	if (Diagnostic.ResolvedTypeface.IsEmpty()
		&& Diagnostic.ResolutionSourceKind != TEXT("unresolved")
		&& !Resolution.bUseMappedAssetDefaultTypeface)
	{
		Diagnostic.ResolvedTypeface = TEXT("Regular");
	}

	return Diagnostic;
}
