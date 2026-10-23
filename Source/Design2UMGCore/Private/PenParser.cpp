// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParser.h"
#include "DesignBrowserURL.h"
#include "DesignComponentMapping.h"
#include "Design2UMGLog.h"
#include "DesignImportTypes.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "DesignJsonUtils.h"
#include "DesignTreeLimits.h"
#include "Misc/DefaultValueHelper.h"
#include "PenParserInternal.h"
#include "PenParserNodeExtensions.h"
#include "ParserSharedSemantics.h"
#include "PenBundledLibraries.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"

namespace
{
// JSON containers are mutable; scalar values are immutable and retain their exact type,
// including explicit null. UE's generic Duplicate drops EJson::Null values.
TSharedPtr<FJsonValue> CopyPencilContainers(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid()) return Value;
	if (Value->Type == EJson::Object)
	{
		auto Object = MakeShared<FJsonObject>();
		for (const auto& Pair : Value->AsObject()->Values) Object->SetField(Pair.Key, CopyPencilContainers(Pair.Value));
		return MakeShared<FJsonValueObject>(Object);
	}
	if (Value->Type == EJson::Array)
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const auto& Item : Value->AsArray()) Items.Add(CopyPencilContainers(Item));
		return MakeShared<FJsonValueArray>(MoveTemp(Items));
	}
	return Value;
}

void AssignMissingNodeIds(const TSharedPtr<FJsonObject>& Node, FString AncestorId, FString RelativePath);

void AssignMissingChildrenIds(const TSharedPtr<FJsonObject>& Parent, const FString& AncestorId, const FString& RelativePath)
{
	const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
	if (!Parent->TryGetArrayField(TEXT("children"), Children)) return;
	for (int32 Index = 0; Index < Children->Num(); ++Index)
	{
		const auto& Child = (*Children)[Index];
		if (Child.IsValid() && Child->Type == EJson::Object)
			AssignMissingNodeIds(Child->AsObject(), AncestorId, RelativePath + FString::Printf(TEXT("/children/%d"), Index));
	}
}

void AssignMissingNodeIds(const TSharedPtr<FJsonObject>& Node, FString AncestorId, FString RelativePath)
{
	if (!Node->HasField(TEXT("id")))
	{
		// Anonymous nodes have structural identity; reordering them changes that identity.
		const FString Identity = AncestorId + TEXT("/") + RelativePath;
		const FTCHARToUTF8 Bytes(*Identity);
		Node->SetStringField(TEXT("id"), TEXT("pen_") + FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()));
	}
	else if (Node->TryGetStringField(TEXT("id"), AncestorId))
	{
		RelativePath.Reset();
	}
	AssignMissingChildrenIds(Node, AncestorId, RelativePath);
	const TSharedPtr<FJsonObject>* Descendants = nullptr;
	if (!Node->TryGetObjectField(TEXT("descendants"), Descendants)) return;
	for (const auto& Pair : (*Descendants)->Values)
	{
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object) continue;
		const auto Fragment = Pair.Value->AsObject();
		const FString OverridePath = RelativePath + TEXT("/descendants/") + FString(*Pair.Key);
		if (Fragment->HasField(TEXT("type"))) AssignMissingNodeIds(Fragment, AncestorId, OverridePath);
		else AssignMissingChildrenIds(Fragment, AncestorId, OverridePath);
	}
}

struct FPenParserParseContext
{
	bool bDepthExceeded = false;
	// Single shared node-count budget for the whole parsed document tree (shares the
	// UMG JSON parser's DesignTreeLimits semantics). A wide-but-shallow document is
	// bounded just like a deep one; when the budget is exceeded the whole parse is
	// rejected rather than returning a truncated tree.
	FDesignParserBudget Budget;
};

// TU-local pipeline functions defined below; cross-TU parse functions are declared in PenParserInternal.h.
struct FPenImportedDocument
{
	FString Alias;
	TSharedPtr<FJsonObject> Document;
};
bool ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FDesignDocument& OutDocument, const FString& SourceFilePath);
bool LoadImports(const TSharedPtr<FJsonObject>& Document, const FString& SourceFilePath, const FString& Prefix, TSet<FString>& ActiveFiles, TArray<FPenImportedDocument>& OutImports);
void AnchorResourceUrls(const TSharedPtr<FJsonObject>& Document, const FString& SourceFilePath);
void ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FDesignVariable>& OutVariables);
void ExpandImportedLibraryVariables(const TArray<FPenImportedDocument>& Imports, TMap<FString, FDesignVariable>& InOutVariables);
void AppendImportedLibraryNodes(const TArray<FPenImportedDocument>& Imports, const FDesignVariableResolver& Resolver, FDesignDocument& Document, FPenParserParseContext& ParseContext);
bool ValidateNodeDepth(const TSharedPtr<FJsonObject>& JsonObj, FPenParserParseContext& ParseContext, int32 Depth);
bool ValidateChildrenDepth(const TSharedPtr<FJsonValue>& ChildrenField, FPenParserParseContext& ParseContext, int32 Depth);
TSharedPtr<FDesignNode> ParseNodeAtDepth(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver, FPenParserParseContext& ParseContext, int32 Depth);
void AppendParsedChildrenAtDepth(const TSharedPtr<FJsonValue>& ChildrenField, const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& OutChildren, FPenParserParseContext& ParseContext, int32 Depth);
}

bool FPenParser::ParseSourceObject(const FString& JsonString, TSharedPtr<FJsonObject>& OutObject)
{
	OutObject.Reset();
	FString Json = JsonString;
	bool bInString = false;
	bool bEscaped = false;
	TCHAR PreviousToken = 0;
	for (int32 Index = 0; Index < Json.Len(); ++Index)
	{
		const TCHAR Character = Json[Index];
		if (bInString)
		{
			if (bEscaped) bEscaped = false;
			else if (Character == TEXT('\\')) bEscaped = true;
			else if (Character == TEXT('"')) bInString = false;
			continue;
		}
		if (Character == TEXT('"')) { bInString = true; PreviousToken = Character; continue; }
		if (Character != TEXT('/') || Index + 1 >= Json.Len())
		{
			if ((Character == TEXT(']') || Character == TEXT('}')) && PreviousToken == TEXT(',')) return false;
			if (!FChar::IsWhitespace(Character)) PreviousToken = Character;
			continue;
		}
		const TCHAR Next = Json[Index + 1];
		int32 End = Index + 2;
		if (Next == TEXT('/'))
		{
			while (End < Json.Len() && Json[End] != TEXT('\r') && Json[End] != TEXT('\n')) ++End;
		}
		else if (Next == TEXT('*'))
		{
			while (End + 1 < Json.Len() && !(Json[End] == TEXT('*') && Json[End + 1] == TEXT('/'))) ++End;
			if (End + 1 >= Json.Len()) return false;
			End += 2;
		}
		else continue;
		// Keep token boundaries and diagnostic line/column positions intact.
		for (int32 Position = Index; Position < End; ++Position)
			if (Json[Position] != TEXT('\r') && Json[Position] != TEXT('\n')) Json[Position] = TEXT(' ');
		Index = End - 1;
	}
	return DesignJsonUtils::TryParseObject(Json, OutObject);
}

bool FPenParser::ParseDocumentFile(const FString& FilePath, FDesignDocument& OutDocument)
{
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *FilePath))
	{
		OutDocument = FDesignDocument();
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to read Pencil document: %s"), *FilePath);
		return false;
	}
	return ParseDocument(Json, OutDocument, FPaths::ConvertRelativePathToFull(FilePath));
}

bool FPenParser::ParseDocument(const FString& JsonString, FDesignDocument& OutDocument, const FString& SourceFilePath)
{
	TSharedPtr<FJsonObject> JsonObj;
	if (!ParseSourceObject(JsonString, JsonObj))
	{
		OutDocument = FDesignDocument();
		UE_LOG(LogDesign2UMG, Error, TEXT("Invalid design file format. Please verify this is a valid .pen file."));
		return false;
	}
	return ParseDocumentObject(JsonObj, OutDocument, SourceFilePath);
}

bool FPenParser::ParseDocumentObject(const TSharedPtr<FJsonObject>& JsonObject, FDesignDocument& OutDocument, const FString& SourceFilePath)
{
	OutDocument = FDesignDocument();
	FDesignDocument ParsedDocument;
	// Source properties stay owned by this parse, independent of a caller-owned DOM.
	TSharedPtr<FJsonObject> DocumentObject;
	if (JsonObject.IsValid())
	{
		DocumentObject = CopyPencilContainers(MakeShared<FJsonValueObject>(JsonObject))->AsObject();
	}
	if (!ParseDocumentInternal(DocumentObject, ParsedDocument, SourceFilePath)) return false;
	OutDocument = MoveTemp(ParsedDocument);
	return true;
}

namespace
{
bool ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FDesignDocument& OutDocument, const FString& SourceFilePath)
{
	if (!JsonObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Design file content is empty or malformed."));
		return false;
	}

	// A snapshot without document headers has no known format version.
	JsonObj->TryGetStringField(TEXT("version"), OutDocument.Version);

	// Document-level provider marker: component identity extraction and component matching use it to distinguish the source.
	OutDocument.Metadata.FindOrAdd(TEXT("source.provider")) = DesignComponentMapping::ProviderPencil;

	AnchorResourceUrls(JsonObj, SourceFilePath);

	// Parse declared fonts.
	const TSharedPtr<FJsonValue>& FontsField = JsonObj->TryGetField(TEXT("fonts"));
	if (FontsField.IsValid() && FontsField->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontsArr = FontsField->AsArray();
		for (const auto& FontVal : FontsArr)
		{
			const TSharedPtr<FJsonObject>& FontObj = FontVal->AsObject();
			if (!FontObj.IsValid())
				continue;
			FDesignFont Font;
			FontObj->TryGetStringField(TEXT("name"), Font.Name);
			FontObj->TryGetStringField(TEXT("url"), Font.Url);
			OutDocument.Fonts.Add(Font);
		}
	}

	// Parse themes.
	const TSharedPtr<FJsonValue>& ThemesField = JsonObj->TryGetField(TEXT("themes"));
	if (ThemesField.IsValid() && ThemesField->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>& ThemesObj = ThemesField->AsObject();
		for (const auto& Pair : ThemesObj->Values)
		{
			if (Pair.Value->Type != EJson::Array)
				continue;
			TArray<FString> Values;
			const TArray<TSharedPtr<FJsonValue>>& JsonValues = Pair.Value->AsArray();
			for (const auto& Val : JsonValues)
				Values.Add(Val->AsString());
			OutDocument.Themes.Add(FString(*Pair.Key), Values);
		}
	}

	// Parse variables.
	const TSharedPtr<FJsonValue>& VarsField = JsonObj->TryGetField(TEXT("variables"));
	if (VarsField.IsValid() && VarsField->Type == EJson::Object)
	{
		ParseVariables(VarsField->AsObject(), OutDocument.Variables);
	}

	TArray<FPenImportedDocument> Imports;
	TSet<FString> ActiveFiles;
	if (!SourceFilePath.IsEmpty()) ActiveFiles.Add(FPaths::ConvertRelativePathToFull(SourceFilePath));
	if (!LoadImports(JsonObj, SourceFilePath, FString(), ActiveFiles, Imports)) return false;
	ExpandImportedLibraryVariables(Imports, OutDocument.Variables);
	if (!SourceFilePath.IsEmpty())
	{
		OutDocument.RecordSourceFilePath(SourceFilePath);
	}

	// Build variable resolver with the document-level active theme (first value
	// of each declared axis); node-level theme attributes override per-axis in ParseNode.
	FDesignVariableResolver Resolver(OutDocument.Variables);
	const TMap<FString, FString> DocumentTheme = FDesignVariableResolver::BuildDocumentTheme(OutDocument.Themes);
	if (DocumentTheme.Num() > 0)
	{
		Resolver.SetThemeContext(DocumentTheme);
	}

	// Parse top-level children.
	FPenParserParseContext ParseContext;
	if (!ValidateChildrenDepth(JsonObj->TryGetField(TEXT("children")), ParseContext, 0))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Design file parsing failed because node nesting exceeds the supported depth."));
		return false;
	}

	AssignMissingChildrenIds(JsonObj, FString(), FString());
	AppendParsedChildrenAtDepth(JsonObj->TryGetField(TEXT("children")), Resolver, OutDocument.Children, ParseContext, 0);
	AppendImportedLibraryNodes(Imports, Resolver, OutDocument, ParseContext);

	if (ParseContext.bDepthExceeded || ParseContext.Budget.WasExceeded())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Design file parsing failed because the document exceeds supported size/depth limits."));
		// All-or-nothing: a size/depth breach must not publish a truncated tree.
		// AppendParsedChildrenAtDepth above may have already partially populated the
		// document, so clear it before failing the whole parse.
		OutDocument = FDesignDocument();
		return false;
	}

	return true;
}

void ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FDesignVariable>& OutVariables)
{
	for (const auto& Pair : JsonObj->Values)
	{
		FDesignVariable Var;
		Var.Name = FString(*Pair.Key);

		const TSharedPtr<FJsonObject>& VarObj = Pair.Value->AsObject();
		if (!VarObj.IsValid())
			continue;
		FString TypeStr;
		VarObj->TryGetStringField(TEXT("type"), TypeStr);

		if (TypeStr == TEXT("boolean"))
			Var.Type = EDesignVariableType::Boolean;
		else if (TypeStr == TEXT("color"))
			Var.Type = EDesignVariableType::Color;
		else if (TypeStr == TEXT("number"))
			Var.Type = EDesignVariableType::Number;
		else
			Var.Type = EDesignVariableType::String;

		// Parse either a scalar value or a themed value array.
		const TSharedPtr<FJsonValue>& ValueField = VarObj->TryGetField(TEXT("value"));
		if (!ValueField.IsValid())
		{
			OutVariables.Add(Var.Name, Var);
			continue;
		}
		if (ValueField->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Values = ValueField->AsArray();
			for (int32 i = 0; i < Values.Num(); ++i)
			{
				const TSharedPtr<FJsonObject>& EntryObj = Values[i]->AsObject();
				if (!EntryObj.IsValid())
					continue;

				FString EntryValue;
				const TSharedPtr<FJsonValue>& Val = EntryObj->TryGetField(TEXT("value"));
				if (Val.IsValid())
				{
					if (Val->Type == EJson::String)
						EntryValue = Val->AsString();
					else if (Val->Type == EJson::Number)
						EntryValue = FString::SanitizeFloat(Val->AsNumber());
					else if (Val->Type == EJson::Boolean)
						EntryValue = Val->AsBool() ? TEXT("true") : TEXT("false");
				}

				// First entry is used as default value.
				if (i == 0)
					Var.Value = EntryValue;

				// Store themed entry.
				FDesignThemeValue TV;
				TV.Value = EntryValue;
				const TSharedPtr<FJsonValue>& ThemeField = EntryObj->TryGetField(TEXT("theme"));
				if (ThemeField.IsValid() && ThemeField->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject>& ThemeObj = ThemeField->AsObject();
					for (const auto& TP : ThemeObj->Values)
					{
						if (TP.Value->Type == EJson::String)
							TV.ThemeMatch.Add(FString(*TP.Key), TP.Value->AsString());
					}
				}
				Var.ThemedValues.Add(MoveTemp(TV));
			}
		}
		else
		{
			if (ValueField->Type == EJson::String)
				Var.Value = ValueField->AsString();
			else if (ValueField->Type == EJson::Number)
				Var.Value = FString::SanitizeFloat(ValueField->AsNumber());
			else if (ValueField->Type == EJson::Boolean)
				Var.Value = ValueField->AsBool() ? TEXT("true") : TEXT("false");
		}

		OutVariables.Add(Var.Name, Var);
	}
}

bool ParsePencilLibraryId(const FString& ImportId, FString& OutLibraryName)
{
	// Built-in library import ids look like "pencil:shadcn.lib.pen". Anything
	// else (relative paths such as "../design-system.lib.pen", other schemes)
	// has no registered variables table and keeps the unresolved-ref behavior.
	FString Rest;
	constexpr int32 PencilPrefixLen = 7; // "pencil:"
	constexpr int32 LibPenSuffixLen = 8; // ".lib.pen"
	if (!ImportId.StartsWith(TEXT("pencil:")) || !ImportId.EndsWith(TEXT(".lib.pen"))
		|| ImportId.Len() <= PencilPrefixLen + LibPenSuffixLen)
	{
		return false;
	}
	Rest = ImportId.RightChop(PencilPrefixLen);
	OutLibraryName = Rest.LeftChop(LibPenSuffixLen);
	return !OutLibraryName.IsEmpty();
}

// Percent decoding is for URI paths, not form data: '+' stays a literal plus.
FString DecodeResourcePath(const FString& Uri)
{
	FTCHARToUTF8 Utf8(*Uri);
	TArray<ANSICHAR> Bytes;
	for (int32 Index = 0; Index < Utf8.Length(); ++Index)
	{
		if (Utf8.Get()[Index] == '%' && Index + 2 < Utf8.Length()
			&& FChar::IsHexDigit(Utf8.Get()[Index + 1]) && FChar::IsHexDigit(Utf8.Get()[Index + 2]))
		{
			Bytes.Add(static_cast<ANSICHAR>((FParse::HexDigit(Utf8.Get()[Index + 1]) << 4) | FParse::HexDigit(Utf8.Get()[Index + 2])));
			Index += 2;
		}
		else Bytes.Add(Utf8.Get()[Index]);
	}
	Bytes.Add(0);
	return FString(UTF8_TO_TCHAR(Bytes.GetData()));
}

FString ResolveResourcePath(const FString& Uri, const FString& SourceFilePath)
{
	if (Uri.IsEmpty() || SourceFilePath.IsEmpty() || Uri.Contains(TEXT(":")) || !FPaths::IsRelative(Uri)) return Uri;
	FString Result = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::ConvertRelativePathToFull(SourceFilePath)), DecodeResourcePath(Uri));
	FPaths::NormalizeFilename(Result);
	return Result;
}

void AnchorResourceValue(TSharedPtr<FJsonValue>& Value, const FString& SourceFilePath)
{
	if (!Value.IsValid()) return;
	if (Value->Type == EJson::Array)
	{
		TArray<TSharedPtr<FJsonValue>> Items = Value->AsArray();
		for (auto& Item : Items) AnchorResourceValue(Item, SourceFilePath);
		Value = MakeShared<FJsonValueArray>(MoveTemp(Items));
	}
	else if (Value->Type == EJson::Object)
	{
		FString Type;
		Value->AsObject()->TryGetStringField(TEXT("type"), Type);
		if (Type == TEXT("script"))
		{
			FString Uri;
			if (Value->AsObject()->TryGetStringField(TEXT("scriptUri"), Uri))
				Value->AsObject()->SetStringField(TEXT("scriptUri"), ResolveResourcePath(Uri, SourceFilePath));
		}
		for (auto& Pair : Value->AsObject()->Values)
		{
			if (Pair.Key == TEXT("metadata")) continue;
			if (Type == TEXT("browser") && Pair.Key == TEXT("url") && Pair.Value->Type == EJson::String)
				Pair.Value = MakeShared<FJsonValueString>(DesignBrowserURL::Resolve(Pair.Value->AsString(), SourceFilePath));
			else if ((Type == TEXT("image") || Type == TEXT("shader")) && Pair.Key == TEXT("url") && Pair.Value->Type == EJson::String)
				Pair.Value = MakeShared<FJsonValueString>(ResolveResourcePath(Pair.Value->AsString(), SourceFilePath));
			else if (Type == TEXT("shader") && Pair.Key == TEXT("uniforms") && Pair.Value->Type == EJson::Object)
            {
                for(auto& Uniform:Pair.Value->AsObject()->Values)
                    if(Uniform.Value->Type==EJson::String && !Uniform.Value->AsString().StartsWith(TEXT("#")) && !Uniform.Value->AsString().StartsWith(TEXT("$")))
                        Uniform.Value=MakeShared<FJsonValueString>(ResolveResourcePath(Uniform.Value->AsString(),SourceFilePath));
            }
            else AnchorResourceValue(Pair.Value, SourceFilePath);
		}
	}
}

void AnchorResourceUrls(const TSharedPtr<FJsonObject>& Document, const FString& SourceFilePath)
{
	if (SourceFilePath.IsEmpty()) return;
	for (const TCHAR* Key : { TEXT("children") })
	{
		TSharedPtr<FJsonValue> Value = Document->TryGetField(Key);
		AnchorResourceValue(Value, SourceFilePath);
		if (Value.IsValid()) Document->SetField(Key, Value);
	}
	const TArray<TSharedPtr<FJsonValue>>* Fonts = nullptr;
	if (Document->TryGetArrayField(TEXT("fonts"), Fonts))
		for (const auto& Font : *Fonts)
		{
			FString Url;
			if (Font->Type == EJson::Object && Font->AsObject()->TryGetStringField(TEXT("url"), Url))
				Font->AsObject()->SetStringField(TEXT("url"), ResolveResourcePath(Url, SourceFilePath));
		}
}

bool LoadImports(const TSharedPtr<FJsonObject>& Document, const FString& SourceFilePath, const FString& Prefix,
	TSet<FString>& ActiveFiles, TArray<FPenImportedDocument>& OutImports)
{
	const TSharedPtr<FJsonObject>* Imports = nullptr;
	if (!Document->TryGetObjectField(TEXT("imports"), Imports)) return true;
	for (const auto& Pair : (*Imports)->Values)
	{
		if (Pair.Value->Type != EJson::String) return false;
		const FString Uri = Pair.Value->AsString();
		FString LibraryName, Json, LibraryPath;
		TSharedPtr<FJsonObject> Library;
		if (ParsePencilLibraryId(Uri, LibraryName))
		{
			if (!PenBundledLibraries::TryGetDocumentJson(LibraryName, Json, &LibraryPath))
			{
				UE_LOG(LogDesign2UMG, Warning, TEXT("Unregistered Pencil library: %s"), *Uri);
				continue;
			}
		}
		else
		{
			LibraryPath = ResolveResourcePath(Uri, SourceFilePath);
			if (SourceFilePath.IsEmpty() || ActiveFiles.Contains(LibraryPath)
				|| !FFileHelper::LoadFileToString(Json, *LibraryPath))
			{
				UE_LOG(LogDesign2UMG, Error, TEXT("Failed to load Pencil import '%s' from '%s'."), *Uri, *SourceFilePath);
				return false;
			}
		}
		if (!FPenParser::ParseSourceObject(Json, Library)) return false;
		// Bound recursive imports using the same document nesting limit.
		if (ActiveFiles.Num() >= DesignTreeLimits::MaxNodeDepth) return false;
		const FString Identity = LibraryPath.IsEmpty() ? Uri : LibraryPath;
		if (ActiveFiles.Contains(Identity)) return false;
		ActiveFiles.Add(Identity);
		const FString Alias = Prefix + FString(*Pair.Key);
		if (!LoadImports(Library, LibraryPath, Alias + TEXT(":"), ActiveFiles, OutImports)) return false;
		ActiveFiles.Remove(Identity);
		AnchorResourceUrls(Library, LibraryPath);
		OutImports.Add({Alias, Library});
	}
	return true;
}

FString QualifyVariable(const FString& Text, const FString& Prefix, const TSharedPtr<FJsonObject>& Variables)
{
	if (!Text.StartsWith(TEXT("$"))) return Text;
	const FString Name = Text.RightChop(1);
	return ((Variables.IsValid() && Variables->HasField(Name)) || Name.Contains(TEXT(":"))) ? TEXT("$") + Prefix + Name : Text;
}

void ExpandImportedLibraryVariables(const TArray<FPenImportedDocument>& Imports, TMap<FString, FDesignVariable>& InOutVariables)
{
	for (const auto& Import : Imports)
	{
		const TSharedPtr<FJsonObject>* Variables = nullptr;
		if (!Import.Document->TryGetObjectField(TEXT("variables"), Variables)) continue;
		TMap<FString, FDesignVariable> Parsed;
		ParseVariables(*Variables, Parsed);
		TMap<FString, FString> DefaultTheme;
		const TSharedPtr<FJsonObject>* Themes = nullptr;
		if (Import.Document->TryGetObjectField(TEXT("themes"), Themes))
			for (const auto& Theme : (*Themes)->Values)
				if (Theme.Value->Type == EJson::Array && !Theme.Value->AsArray().IsEmpty()) DefaultTheme.Add(FString(*Theme.Key), Theme.Value->AsArray()[0]->AsString());
		for (auto& Pair : Parsed)
		{
			FDesignVariable& Variable = Pair.Value;
			for (const auto& Entry : Variable.ThemedValues)
			{
				bool Matches = true;
				for (const auto& Axis : Entry.ThemeMatch)
					if (DefaultTheme.FindRef(Axis.Key) != Axis.Value) Matches = false;
				if (Matches) Variable.Value = Entry.Value;
			}
			const FString Prefix = Import.Alias + TEXT(":");
			Variable.Value = QualifyVariable(Variable.Value, Prefix, *Variables);
			for (auto& Entry : Variable.ThemedValues) Entry.Value = QualifyVariable(Entry.Value, Prefix, *Variables);
			Variable.Name = Prefix + Pair.Key;
			Variable.SourceAlias = Import.Alias;
			const FString Name = Variable.Name;
			if (!InOutVariables.Contains(Name)) InOutVariables.Add(Name, MoveTemp(Variable));
		}
	}
}

void QualifyLibraryVariables(TSharedPtr<FJsonValue>& Value, const FString& Prefix, const TSharedPtr<FJsonObject>& Variables)
{
	if (!Value.IsValid()) return;
	if (Value->Type == EJson::Array)
	{
		TArray<TSharedPtr<FJsonValue>> Items = Value->AsArray();
		for (auto& Item : Items) QualifyLibraryVariables(Item, Prefix, Variables);
		Value = MakeShared<FJsonValueArray>(MoveTemp(Items));
	}
	else if (Value->Type == EJson::Object)
	{
		for (auto& Pair : Value->AsObject()->Values) QualifyLibraryVariables(Pair.Value, Prefix, Variables);
	}
	else if (Value->Type == EJson::String)
	{
		Value = MakeShared<FJsonValueString>(QualifyVariable(Value->AsString(), Prefix, Variables));
	}
}

// Identity fields belong to nodes and descendant patches, not arbitrary property objects.
void QualifyLibraryNodes(TSharedPtr<FJsonValue>& Value, const FString& Prefix, const TSharedPtr<FJsonObject>& Variables)
{
	if (!Value.IsValid()) return;
	if (Value->Type == EJson::Array)
	{
		TArray<TSharedPtr<FJsonValue>> Children = Value->AsArray();
		for (auto& Child : Children) QualifyLibraryNodes(Child, Prefix, Variables);
		Value = MakeShared<FJsonValueArray>(MoveTemp(Children));
	}
	else if (Value->Type == EJson::Object)
	{
		for (auto& Pair : Value->AsObject()->Values)
		{
			if (Pair.Key == TEXT("metadata")) continue;
			if (Pair.Key == TEXT("children"))
			{
				QualifyLibraryNodes(Pair.Value, Prefix, Variables);
				continue;
			}
			if (Pair.Key == TEXT("descendants") && Pair.Value->Type == EJson::Object)
			{
				FJsonObject Qualified;
				for (auto& Override : Pair.Value->AsObject()->Values)
				{
					TArray<FString> Segments;
					FString(*Override.Key).ParseIntoArray(Segments, TEXT("/"));
					for (FString& Segment : Segments) Segment = Prefix + Segment;
					QualifyLibraryNodes(Override.Value, Prefix, Variables);
					Qualified.SetField(FString::Join(Segments, TEXT("/")), Override.Value);
				}
				Pair.Value->AsObject()->Values = MoveTemp(Qualified.Values);
				continue;
			}
			if (Pair.Value->Type == EJson::String && (Pair.Key == TEXT("id") || Pair.Key == TEXT("ref")))
			{
				Pair.Value = MakeShared<FJsonValueString>(Prefix + Pair.Value->AsString());
			}
			else if (Pair.Key == TEXT("slot") && Pair.Value->Type == EJson::Array)
			{
				TArray<TSharedPtr<FJsonValue>> Slots;
				for (const auto& Slot : Pair.Value->AsArray()) Slots.Add(MakeShared<FJsonValueString>(Prefix + Slot->AsString()));
				Pair.Value = MakeShared<FJsonValueArray>(MoveTemp(Slots));
			}
			else
			{
				if (Pair.Key == TEXT("theme") && Pair.Value->Type == EJson::Object)
				{
					FJsonObject Theme;
					for (const auto& Axis : Pair.Value->AsObject()->Values) Theme.SetField(Prefix + FString(*Axis.Key), Axis.Value);
					Pair.Value->AsObject()->Values = MoveTemp(Theme.Values);
				}
				QualifyLibraryVariables(Pair.Value, Prefix, Variables);
			}
		}
	}
}

void AppendImportedLibraryNodes(const TArray<FPenImportedDocument>& Imports, const FDesignVariableResolver& Resolver,
	FDesignDocument& Document, FPenParserParseContext& ParseContext)
{
	for (const auto& Import : Imports)
	{
		const TSharedPtr<FJsonObject>& Library = Import.Document;
		const TArray<TSharedPtr<FJsonValue>>* Fonts = nullptr;
		if (Library->TryGetArrayField(TEXT("fonts"), Fonts))
			for (const auto& Value : *Fonts)
			{
				if (Value->Type != EJson::Object) continue;
				FDesignFont Font;
				Value->AsObject()->TryGetStringField(TEXT("name"), Font.Name);
				Value->AsObject()->TryGetStringField(TEXT("url"), Font.Url);
				Document.Fonts.Add(MoveTemp(Font));
			}
		TSharedPtr<FJsonValue> Children = Library->TryGetField(TEXT("children"));
		if (!ValidateChildrenDepth(Children, ParseContext, 0)) return;
		AssignMissingChildrenIds(Library, FString(), FString());
		const TSharedPtr<FJsonObject>* Variables = nullptr;
		Library->TryGetObjectField(TEXT("variables"), Variables);
		QualifyLibraryNodes(Children, Import.Alias + TEXT(":"), Variables ? *Variables : nullptr);
		TArray<TSharedPtr<FDesignNode>> LibraryNodes;
		AppendParsedChildrenAtDepth(Children, Resolver, LibraryNodes, ParseContext, 0);
		Document.MutableSourceDocument().ImportedNodes.Append(MoveTemp(LibraryNodes));
	}
}

bool ValidateNodeDepth(
	const TSharedPtr<FJsonObject>& JsonObj,
	FPenParserParseContext& ParseContext,
	int32 Depth)
{
	if (!JsonObj.IsValid())
		return true;

	if (Depth > DesignTreeLimits::MaxNodeDepth)
	{
		ParseContext.bDepthExceeded = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document children nesting is too deep (over %d levels)."), DesignTreeLimits::MaxNodeDepth);
		return false;
	}

	if (!ValidateChildrenDepth(JsonObj->TryGetField(TEXT("children")), ParseContext, Depth + 1))
		return false;

	const TSharedPtr<FJsonValue> DescendantsField = JsonObj->TryGetField(TEXT("descendants"));
	if (!DescendantsField.IsValid() || DescendantsField->Type != EJson::Object)
		return true;

	for (const auto& Pair : DescendantsField->AsObject()->Values)
	{
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
			continue;

		const TSharedPtr<FJsonObject> DescendantObj = Pair.Value->AsObject();
		if (!DescendantObj.IsValid())
			continue;

		const bool bFullNodeOverride = DescendantObj->HasField(TEXT("type"));
		if (bFullNodeOverride)
		{
			if (!ValidateNodeDepth(DescendantObj, ParseContext, 0))
				return false;
		}
		else if (!ValidateChildrenDepth(DescendantObj->TryGetField(TEXT("children")), ParseContext, 0))
		{
			return false;
		}
	}

	return true;
}

bool ValidateChildrenDepth(
	const TSharedPtr<FJsonValue>& ChildrenField,
	FPenParserParseContext& ParseContext,
	int32 Depth)
{
	if (!ChildrenField.IsValid() || ChildrenField->Type != EJson::Array)
		return true;

	if (Depth > DesignTreeLimits::MaxNodeDepth)
	{
		ParseContext.bDepthExceeded = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document children nesting is too deep (over %d levels)."), DesignTreeLimits::MaxNodeDepth);
		return false;
	}

	for (const TSharedPtr<FJsonValue>& ChildVal : ChildrenField->AsArray())
	{
		if (ChildVal.IsValid() && ChildVal->Type == EJson::Object
			&& !ValidateNodeDepth(ChildVal->AsObject(), ParseContext, Depth))
		{
			return false;
		}
	}

	return true;
}

TSharedPtr<FDesignNode> ParseNodeAtDepth(
	const TSharedPtr<FJsonObject>& JsonObj,
	const FDesignVariableResolver& Resolver,
	FPenParserParseContext& ParseContext,
	int32 Depth)
{
	if (!JsonObj.IsValid())
		return nullptr;

	if (Depth > DesignTreeLimits::MaxNodeDepth)
	{
		ParseContext.bDepthExceeded = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document nesting is too deep (over %d levels); node skipped."), DesignTreeLimits::MaxNodeDepth);
		return nullptr;
	}

	// Whole-tree node-count budget: one shared counting context across the document.
	if (!ParseContext.Budget.TryEnterNode(Depth))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document exceeds the maximum parsed node count (%d); parsing aborted."), DesignTreeLimits::MaxParsedNodeCount);
		return nullptr;
	}

    TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
    Node->Metadata.Add(TEXT("source.provider"), DesignComponentMapping::ProviderPencil);

	// Basic node fields.
	if (!JsonObj->TryGetStringField(TEXT("id"), Node->Id))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Design file contains a node without id; skipped."));
		return nullptr;
	}
	JsonObj->TryGetStringField(TEXT("name"), Node->Name);
	FString TypeStr;
	if (!JsonObj->TryGetStringField(TEXT("type"), TypeStr))
	{
		UE_LOG(LogDesign2UMG, Warning, TEXT("Node '%s' is missing type metadata and was skipped."), *Node->Id);
		return nullptr;
	}
	Node->Type = PenParser::ParseNodeType(TypeStr);
	Node->Shape.bFitGeometryToBounds = Node->Type == EDesignNodeType::Path;
	Node->MutableSource().SourceNodeType = TypeStr;

	// Node-level theme overrides for variable resolving.
	const FDesignVariableResolver* ActiveResolver = &Resolver;
	FDesignVariableResolver ThemedResolver;
	const TSharedPtr<FJsonValue>& ThemeNodeField = JsonObj->TryGetField(TEXT("theme"));
	if (ThemeNodeField.IsValid() && ThemeNodeField->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>& ThemeObj = ThemeNodeField->AsObject();
		for (const auto& Pair : ThemeObj->Values)
		{
			if (Pair.Value->Type == EJson::String)
				Node->Theme.Add(FString(*Pair.Key), Pair.Value->AsString());
		}
		if (Node->Theme.Num() > 0)
		{
			ThemedResolver = Resolver;
			ThemedResolver.PushThemeOverride(Node->Theme);
			ActiveResolver = &ThemedResolver;
		}
	}

	PenParser::ParseNodeProperties(JsonObj, Node, *ActiveResolver);
	if (Node->Type == EDesignNodeType::Note || Node->Type == EDesignNodeType::Context || Node->Type == EDesignNodeType::Prompt)
	{
		PenParserNodeExtensions::SynthesizeStickyCard(Node);
	}

	AppendParsedChildrenAtDepth(JsonObj->TryGetField(TEXT("children")), *ActiveResolver, Node->Children, ParseContext, Depth + 1);
	PenParserNodeExtensions::FinalizePenPaddingRuntimeWrapPolicy(Node);

	PenParserNodeExtensions::ParsePenSemanticMetadata(JsonObj, *Node, *ActiveResolver);
	if (Node->Type == EDesignNodeType::Ref)
	{
		Node->RefOverrides = MakeShared<FDesignNodePatch>(PenParser::CaptureNodePatch(JsonObj, *Node, *ActiveResolver));
	}

	return Node;
}

void AppendParsedChildrenAtDepth(
	const TSharedPtr<FJsonValue>& ChildrenField,
	const FDesignVariableResolver& Resolver,
	TArray<TSharedPtr<FDesignNode>>& OutChildren,
	FPenParserParseContext& ParseContext,
	int32 Depth)
{
	if (!ChildrenField.IsValid() || ChildrenField->Type != EJson::Array)
		return;

	if (Depth > DesignTreeLimits::MaxNodeDepth)
	{
		ParseContext.bDepthExceeded = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document children nesting is too deep (over %d levels); remaining children skipped."), DesignTreeLimits::MaxNodeDepth);
		return;
	}

	for (const TSharedPtr<FJsonValue>& ChildVal : ChildrenField->AsArray())
	{
		if (TSharedPtr<FDesignNode> Child = ParseNodeAtDepth(ChildVal->AsObject(), Resolver, ParseContext, Depth))
		{
			OutChildren.Add(Child);
		}
		else if (ParseContext.Budget.WasExceeded() || ParseContext.bDepthExceeded)
		{
			// Both latches stay set once tripped, so every remaining sibling would fail
			// and log the identical error again. The whole parse is already rejected at
			// this point (ParseDocumentInternal clears the document), so stop walking
			// instead of amplifying CPU and log cost across a potentially huge sibling
			// list. Note this is reached only when the failure was a limit breach -- a
			// node rejected for any other reason does not stop its siblings.
			return;
		}
	}
}
}

namespace PenParser
{
void ParseNodeProperties(const TSharedPtr<FJsonObject>& JsonObj, TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& InResolver)
{
	FDesignVariableResolver Resolver = InResolver;
	const TSharedPtr<FJsonObject>* Theme = nullptr;
	if (JsonObj->TryGetObjectField(TEXT("theme"), Theme))
	{
		for (const auto& Pair : (*Theme)->Values)
		{
			FString Value;
			if (Pair.Value->TryGetString(Value)) Node->Theme.Add(FString(*Pair.Key), Value);
		}
		Resolver.PushThemeOverride(Node->Theme);
	}
	const TSharedPtr<FJsonValue>& SizeField = JsonObj->TryGetField(TEXT("size"));
	if (SizeField.IsValid() && SizeField->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>& SizeObject = SizeField->AsObject();
		float SizeWidth = 0.0f;
		if (TryGetResolvedNumberField(SizeObject, TEXT("width"), Resolver, SizeWidth))
		{
			Node->Transform.SetWidthValue(FDesignSize::Fixed(SizeWidth));
		}

		float SizeHeight = 0.0f;
		if (TryGetResolvedNumberField(SizeObject, TEXT("height"), Resolver, SizeHeight))
		{
			Node->Transform.SetHeightValue(FDesignSize::Fixed(SizeHeight));
		}
	}

	PenParser::ApplyNodeBoxProperties(JsonObj, *Node, Node->Type, Resolver);
	Node->Transform.TransformOrigin = FVector2D::ZeroVector;
	TryGetResolvedNumberField(JsonObj, TEXT("rotation"), Resolver, Node->Transform.Rotation);

	// Visibility.
	TryGetResolvedBoolField(JsonObj, TEXT("enabled"), Resolver, Node->Transform.bEnabled);
	TryGetResolvedBoolField(JsonObj, TEXT("flipX"), Resolver, Node->Transform.bFlipX);
	TryGetResolvedBoolField(JsonObj, TEXT("flipY"), Resolver, Node->Transform.bFlipY);

	// Groups and script output retain child-local coordinates instead of flow layout.
	{
		FString LayoutStr;
		if (JsonObj->TryGetStringField(TEXT("layout"), LayoutStr))
			Node->LayoutData.Layout = PenParser::ParseLayout(LayoutStr);
		else if (Node->Type == EDesignNodeType::Group || Node->Type == EDesignNodeType::Script)
			Node->LayoutData.Layout = EDesignLayout::None;
	}
	PenParser::ApplyNodeLayoutProperties(JsonObj, *Node, Resolver);
	if (JsonObj->HasField(TEXT("layoutIncludeStroke")))
	{
		bool bLayoutIncludeStroke = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("layoutIncludeStroke"), Resolver, bLayoutIncludeStroke))
		{
			Node->Metadata.Add(TEXT("layoutIncludeStroke"), bLayoutIncludeStroke ? TEXT("true") : TEXT("false"));
		}
	}
	TryGetResolvedNumberField(JsonObj, TEXT("layoutGrow"), Resolver, Node->LayoutData.LayoutGrow);

	// Visual properties.
	if (JsonObj->HasField(TEXT("fill")))
		PenParser::ParseNodeFillProperties(JsonObj->TryGetField(TEXT("fill")), *Node, Resolver);
	PenParser::ParseNodeStrokeProperties(JsonObj, *Node, Resolver);
	if (JsonObj->HasField(TEXT("cornerRadius")))
		Node->Visual.CornerRadius = Resolver.ResolveCornerRadius(JsonObj->TryGetField(TEXT("cornerRadius")));

	PenParserNodeExtensions::ParseNodeExtended(JsonObj, Node, Resolver);
}

bool ParseGeneratedNodes(const TArray<TSharedPtr<FJsonValue>>& Values, const FString& SourcePath,
	const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& Out, FString& Error)
{
	Out.Reset();
	auto Document = MakeShared<FJsonObject>();
	Document->SetArrayField(TEXT("children"), Values);
	AnchorResourceUrls(Document, SourcePath);
	FPenParserParseContext Context;
	for (const auto& Value : Document->GetArrayField(TEXT("children")))
	{
		auto Node = ParseNodeAtDepth(Value->AsObject(), Resolver, Context, 0);
		if (!Node.IsValid()) { Error = TEXT("Generated script node parsing failed."); Out.Reset(); return false; }
		Out.Add(Node);
	}
	if (Context.bDepthExceeded || Context.Budget.WasExceeded())
	{
		Error = TEXT("Generated script nodes exceed the parser budget."); Out.Reset(); return false;
	}
	return true;
}

TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver)
{
	FPenParserParseContext ParseContext;
	return ParseNodeAtDepth(JsonObj, Resolver, ParseContext, 0);
}

void AppendParsedChildren(
	const TSharedPtr<FJsonValue>& ChildrenField,
	const FDesignVariableResolver& Resolver,
	TArray<TSharedPtr<FDesignNode>>& OutChildren)
{
	FPenParserParseContext ParseContext;
	AppendParsedChildrenAtDepth(ChildrenField, Resolver, OutChildren, ParseContext, 0);
}

void ParseNodeFillProperties(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver)
{
	OutNode.Visual.Fills.Empty();
	OutNode.Visual.bFillExplicitClear = false;
	if (Value.IsValid() && Value->Type == EJson::Array)
	{
		OutNode.Visual.bFillExplicitClear = Value->AsArray().Num() == 0;
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			FDesignFill Fill = ParseFill(Item, Resolver);
			if (Fill.Type != EDesignFillType::None && Fill.bEnabled)
				OutNode.Visual.Fills.Add(MoveTemp(Fill));
		}
	}
	else
	{
		FDesignFill Fill = ParseFill(Value, Resolver);
		OutNode.Visual.bFillExplicitClear = Fill.bExplicitClear;
		if (Fill.Type != EDesignFillType::None && Fill.bEnabled)
		{
			OutNode.Visual.Fills.Add(MoveTemp(Fill));
		}
	}
}

namespace
{
	EDesignStrokeAlign ParsePenStrokeAlign(const FString& Align)
	{
		if (Align == TEXT("center"))
		{
			return EDesignStrokeAlign::Center;
		}
		if (Align == TEXT("outer"))
		{
			return EDesignStrokeAlign::Outside;
		}
		return EDesignStrokeAlign::Inside;
	}

	bool TryParsePenStrokeThickness(
		const TSharedPtr<FJsonValue>& Value,
		const FDesignVariableResolver& Resolver,
		FDesignStrokeThickness& OutThickness)
	{
		if (!Value.IsValid())
		{
			return false;
		}

		float Uniform = 0.0f;
		if (TryGetNumberValueFlexible(Value, Uniform))
		{
			OutThickness.bUniform = true;
			OutThickness.Uniform = Uniform;
			return true;
		}

		if (Value->Type == EJson::String && FDesignVariableResolver::IsVariableRef(Value->AsString()))
		{
			OutThickness.bUniform = true;
			OutThickness.Uniform = Resolver.ResolveNumber(Value->AsString());
			return true;
		}

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> ThicknessObj = Value->AsObject();
			if (!ThicknessObj.IsValid())
			{
				return false;
			}

			FDesignStrokeThickness ParsedThickness;
			ParsedThickness.bUniform = false;
			bool bParsedSide = false;
			bParsedSide |= TryGetResolvedNumberField(ThicknessObj, TEXT("top"), Resolver, ParsedThickness.Top);
			bParsedSide |= TryGetResolvedNumberField(ThicknessObj, TEXT("right"), Resolver, ParsedThickness.Right);
			bParsedSide |= TryGetResolvedNumberField(ThicknessObj, TEXT("bottom"), Resolver, ParsedThickness.Bottom);
			bParsedSide |= TryGetResolvedNumberField(ThicknessObj, TEXT("left"), Resolver, ParsedThickness.Left);
			if (bParsedSide)
			{
				OutThickness = ParsedThickness;
			}
			return bParsedSide;
		}

		return false;
	}
}

void ParseNodeStrokeProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver)
{
	if (!JsonObj.IsValid())
		return;

	const TSharedPtr<FJsonValue> StrokeField = JsonObj->TryGetField(TEXT("stroke"));
	FDesignStroke Stroke;
	Stroke.Align = EDesignStrokeAlign::Center;
	bool bHasCurrentStroke = false;

	FString StrokeAlignment;
	if (JsonObj->TryGetStringField(TEXT("strokeAlignment"), StrokeAlignment))
	{
		Stroke.Align = ParsePenStrokeAlign(StrokeAlignment);
		bHasCurrentStroke = true;
	}

	FString StrokeLinecap;
	if (JsonObj->TryGetStringField(TEXT("strokeLinecap"), StrokeLinecap))
	{
		OutNode.Metadata.Add(ParserSharedSemantics::StrokeCapMetadataKey, StrokeLinecap);
		EDesignStrokeCap ParsedCap;
		if (ParserSharedSemantics::TryParseStrokeCapToken(StrokeLinecap, ParsedCap))
		{
			Stroke.Cap = ParsedCap;
			bHasCurrentStroke = true;
		}
	}

	FString StrokeLinejoin;
	if (JsonObj->TryGetStringField(TEXT("strokeLinejoin"), StrokeLinejoin))
	{
		OutNode.Metadata.Add(ParserSharedSemantics::StrokeJoinMetadataKey, StrokeLinejoin);
		EDesignStrokeJoin ParsedJoin;
		if (ParserSharedSemantics::TryParseStrokeJoinToken(StrokeLinejoin, ParsedJoin))
		{
			Stroke.Join = ParsedJoin;
			bHasCurrentStroke = true;
		}
	}

	if (TryParsePenStrokeThickness(JsonObj->TryGetField(TEXT("strokeWidth")), Resolver, Stroke.Thickness))
	{
		bHasCurrentStroke = true;
	}

	if (StrokeField.IsValid())
	{
		FDesignFill StrokeFill = ParseFill(StrokeField, Resolver);
		if (StrokeFill.Type != EDesignFillType::None || StrokeFill.bExplicitClear)
		{
			Stroke.Fill = StrokeFill;
			bHasCurrentStroke = true;
		}
	}

	if (bHasCurrentStroke)
	{
		OutNode.Visual.Stroke = Stroke;
	}
}

FDesignSize ParseSize(const TSharedPtr<FJsonValue>& Value, EDesignNodeType NodeType, const FDesignVariableResolver& Resolver)
{
	if (!Value.IsValid())
		return FDesignSize::Fit();

	// Numeric value.
	if (Value->Type == EJson::Number)
	{
		float NumVal = Value->AsNumber();
		// line node special case: zero is a valid fixed size.
		if (NumVal == 0 && NodeType != EDesignNodeType::Line)
			return FDesignSize::Fit();
		return FDesignSize::Fixed(NumVal);
	}

	// String value.
	if (Value->Type == EJson::String)
	{
		FString Str = Value->AsString();

		// Variable reference.
		if (FDesignVariableResolver::IsVariableRef(Str))
		{
			float Resolved = Resolver.ResolveNumber(Str);
			// Keep behavior aligned with numeric path: 0 => Fit(), except line nodes.
			if (Resolved == 0 && NodeType != EDesignNodeType::Line)
				return FDesignSize::Fit();
			return FDesignSize::Fixed(Resolved);
		}

		// fill_container / fill_container(N)
		if (Str.StartsWith(TEXT("fill_container")))
		{
			float Fallback = 0.0f;
			int32 Start = Str.Find(TEXT("("));
			if (Start != INDEX_NONE)
			{
				int32 End = Str.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start + 1);
				if (End != INDEX_NONE)
				{
					FString NumStr = Str.Mid(Start + 1, End - Start - 1);
					Fallback = FCString::Atof(*NumStr);
				}
			}
			return FDesignSize::Fill(Fallback);
		}

		// fit_content / fit_content(N)
		if (Str.StartsWith(TEXT("fit_content")))
		{
			float Min = 0.0f;
			int32 Start = Str.Find(TEXT("("));
			if (Start != INDEX_NONE)
			{
				int32 End = Str.Find(TEXT(")"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Start + 1);
				if (End != INDEX_NONE)
				{
					FString NumStr = Str.Mid(Start + 1, End - Start - 1);
					Min = FCString::Atof(*NumStr);
				}
			}
			return FDesignSize::Fit(Min);
		}

		// Try parsing as numeric string.
		float NumVal = FCString::Atof(*Str);
		if (NumVal > 0)
			return FDesignSize::Fixed(NumVal);
	}

	return FDesignSize::Fit();
}
}

TArray<FDesignFrameInfo> ExtractTopFrames(const FDesignDocument& Document)
{
	TArray<FDesignFrameInfo> Result;
	for (const auto& Child : Document.Children)
	{
		if (!Child.IsValid() || Child->Type != EDesignNodeType::Frame)
			continue;

		FDesignFrameInfo Info;
		Info.Id = Child->Id;
		Info.Name = Child->Name.IsEmpty() ? Child->Id : Child->Name;
		Info.Width = Child->Transform.Width.IsFixed() ? Child->Transform.Width.Value : 0.0f;
		Info.Height = Child->Transform.Height.IsFixed() ? Child->Transform.Height.Value : 0.0f;
		Result.Add(MoveTemp(Info));
	}
	return Result;
}
