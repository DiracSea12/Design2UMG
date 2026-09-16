// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGLog.h"
#include "DesignLocalFontImport.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Parse.h"

namespace
{
	void RunBuildFontFamilyCommand(const TArray<FString>& Args)
	{
		// Console input is whitespace-split before it reaches us; re-join the
		// tokens so FParse::Value can honor quoted values (Ttf="C:/My Fonts/F.ttf")
		// the same way the command line does.
		const FString ArgLine = FString::Join(Args, TEXT(" "));

		FString FontPackageName;
		FString TypefaceName;
		FString TtfPath;
		const bool bHaveFont = FParse::Value(*ArgLine, TEXT("Font="), FontPackageName);
		const bool bHaveTypeface = FParse::Value(*ArgLine, TEXT("Typeface="), TypefaceName);
		const bool bHaveTtf = FParse::Value(*ArgLine, TEXT("Ttf="), TtfPath);

		if (!bHaveFont || FontPackageName.IsEmpty() ||
			!bHaveTypeface || TypefaceName.IsEmpty() ||
			!bHaveTtf || TtfPath.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Usage: Design2UMG.BuildFontFamily Font=/Game/Path/MyFont Typeface=SemiBold Ttf=\"C:/Absolute/Font.ttf\""));
			return;
		}

		if (!IFileManager::Get().FileExists(*TtfPath))
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("BuildFontFamily: TTF file not found: %s"), *TtfPath);
			return;
		}

		DesignLocalFontImport::FFace Face;
		Face.Path = TtfPath;
		FString Error;
		const bool bSaved = DesignLocalFontImport::ImportFace(Face, FontPackageName, Error, TypefaceName);
		if (!bSaved)
		{
			UE_LOG(LogDesign2UMG, Error, TEXT("BuildFontFamily: %s"), *Error);
		}
	}

	FAutoConsoleCommand GBuildFontFamilyCommand(
		TEXT("Design2UMG.BuildFontFamily"),
		TEXT("Create or update a composite UFont family asset: Design2UMG.BuildFontFamily Font=/Game/Path/MyFont Typeface=SemiBold Ttf=\"C:/Absolute/Font.ttf\""),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunBuildFontFamilyCommand));
}
