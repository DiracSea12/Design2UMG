// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "FontResolver.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "PenSchema.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Font.h"

#define LOCTEXT_NAMESPACE "FontResolver"

TArray<FString> FFontResolver::FindMissingFonts(const FPenDocument& Document)
{
	TSet<FString> UsedFamilies;

	// Collect from Document.Fonts[]
	for (const FPenFont& Font : Document.Fonts)
	{
		if (!Font.Name.IsEmpty())
			UsedFamilies.Add(Font.Name);
	}

	// Collect from node tree
	CollectFontFamilies(Document.Children, UsedFamilies);

	if (UsedFamilies.Num() == 0)
		return {};

	// Scan project font assets
	TSet<FString> ProjectFonts = ScanProjectFontAssets();

	// Find missing
	TArray<FString> Missing;
	for (const FString& Family : UsedFamilies)
	{
		if (!ProjectFonts.Contains(Family))
			Missing.Add(Family);
	}

	if (Missing.Num() > 0)
	{
		FString List = FString::Join(Missing, TEXT(", "));
		UE_LOG(LogDesign2UMG, Warning, TEXT("缺失字体: %s"), *List);
		FDesign2UMGNotification::Warning(
			FText::Format(LOCTEXT("MissingFonts", "Design2UMG: {0} font(s) not found in project: {1}"),
				FText::AsNumber(Missing.Num()), FText::FromString(List)));
	}

	return Missing;
}

void FFontResolver::CollectFontFamilies(const TArray<TSharedPtr<FPenNode>>& Nodes, TSet<FString>& OutFamilies)
{
	for (const auto& Node : Nodes)
	{
		if (!Node.IsValid()) continue;

		if (!Node->FontFamily.IsEmpty())
			OutFamilies.Add(Node->FontFamily);
		if (!Node->IconFontFamily.IsEmpty())
			OutFamilies.Add(Node->IconFontFamily);

		CollectFontFamilies(Node->Children, OutFamilies);
	}
}

TSet<FString> FFontResolver::ScanProjectFontAssets()
{
	TSet<FString> Result;
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetData> FontAssets;
	AR.GetAssetsByClass(UFont::StaticClass()->GetClassPathName(), FontAssets, true);

	for (const FAssetData& Asset : FontAssets)
	{
		Result.Add(Asset.AssetName.ToString());
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
