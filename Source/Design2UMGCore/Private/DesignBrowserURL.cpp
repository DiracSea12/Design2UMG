// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignBrowserURL.h"
#include "Misc/Paths.h"

namespace
{
	FString EncodePath(const FString& Path, bool bKeepEscapes)
	{
		const FTCHARToUTF8 Bytes(*Path);
		FString Result;
		for (int32 Index = 0; Index < Bytes.Length(); ++Index)
		{
			const uint8 Byte = static_cast<uint8>(Bytes.Get()[Index]);
			if (bKeepEscapes && Byte == '%' && Index + 2 < Bytes.Length()
				&& FChar::IsHexDigit(Bytes.Get()[Index + 1]) && FChar::IsHexDigit(Bytes.Get()[Index + 2]))
			{
				Result.AppendChar('%');
				Result.AppendChar(Bytes.Get()[++Index]);
				Result.AppendChar(Bytes.Get()[++Index]);
			}
			else if ((Byte >= 'a' && Byte <= 'z') || (Byte >= 'A' && Byte <= 'Z')
				|| (Byte >= '0' && Byte <= '9') || Byte == '-' || Byte == '_' || Byte == '.' || Byte == '~' || Byte == '/' || Byte == ':')
				Result.AppendChar(static_cast<TCHAR>(Byte));
			else Result += FString::Printf(TEXT("%%%02X"), Byte);
		}
		return Result;
	}
}

FString DesignBrowserURL::Resolve(const FString& URL, const FString& SourceFilePath)
{
	if (URL.IsEmpty()) return URL;
	int32 Colon;
	if (URL.FindChar(':', Colon) && Colon != 1)
	{
		bool bScheme = Colon > 0 && FChar::IsAlpha(URL[0]);
		for (int32 Index = 1; Index < Colon && bScheme; ++Index)
			bScheme = FChar::IsAlnum(URL[Index]) || URL[Index] == '+' || URL[Index] == '-' || URL[Index] == '.';
		if (bScheme) return URL;
	}
	const bool bAbsolute = !FPaths::IsRelative(URL);
	if (!bAbsolute && SourceFilePath.IsEmpty()) return URL;
	int32 Suffix = URL.Len();
	int32 Found;
	if (URL.FindChar('?', Found)) Suffix = FMath::Min(Suffix, Found);
	if (URL.FindChar('#', Found)) Suffix = FMath::Min(Suffix, Found);
	FString Path = EncodePath(URL.Left(Suffix), true);
	if (!bAbsolute)
		Path = EncodePath(FPaths::GetPath(FPaths::ConvertRelativePathToFull(SourceFilePath)), false) / Path;
	FPaths::CollapseRelativeDirectories(Path);
	return (Path.StartsWith(TEXT("/")) ? TEXT("file://") : TEXT("file:///")) + Path + URL.Mid(Suffix);
}
