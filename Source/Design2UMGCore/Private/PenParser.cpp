// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParser.h"
#include "DesignComponentMapping.h"
#include "Design2UMGLog.h"
#include "DesignImportTypes.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "DesignTreeLimits.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/Paths.h"
#include "PenParserInternal.h"
#include "PenParserNodeExtensions.h"
#include "ParserSharedSemantics.h"
#include "PenBundledLibraries.h"

namespace
{
struct FPenParserParseContext
{
	bool bDepthExceeded = false;
};

// TU-local pipeline functions defined below; cross-TU parse functions are declared in PenParserInternal.h.
bool ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FDesignDocument& OutDocument);
void ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FDesignVariable>& OutVariables);
void ExpandImportedLibraryVariables(const TSharedPtr<FJsonObject>& ImportsObj, TMap<FString, FDesignVariable>& InOutVariables);
bool ValidateNodeDepth(const TSharedPtr<FJsonObject>& JsonObj, FPenParserParseContext& ParseContext, int32 Depth);
bool ValidateChildrenDepth(const TSharedPtr<FJsonValue>& ChildrenField, FPenParserParseContext& ParseContext, int32 Depth);
TSharedPtr<FDesignNode> ParseNodeAtDepth(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver, FPenParserParseContext& ParseContext, int32 Depth);
void AppendParsedChildrenAtDepth(const TSharedPtr<FJsonValue>& ChildrenField, const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& OutChildren, FPenParserParseContext& ParseContext, int32 Depth);
}

bool FPenParser::ParseFromFile(const FString& FilePath, FDesignDocument& OutDocument)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to read design file. Verify the path is valid and not locked: %s"), *FilePath);
		return false;
	}
	const bool bSuccess = ParseDocument(JsonString, OutDocument);
	if (bSuccess)
	{
		const FString ResolvedFilePath = FPaths::ConvertRelativePathToFull(FilePath);
		OutDocument.MutableSourceDocument().SourceFilePath = ResolvedFilePath;
		OutDocument.Metadata.FindOrAdd(TEXT("design.sourceFilePath")) = ResolvedFilePath;
		OutDocument.Metadata.FindOrAdd(TEXT("source.filePath")) = ResolvedFilePath;
	}
	return bSuccess;
}

bool FPenParser::ParseDocument(const FString& JsonString, FDesignDocument& OutDocument)
{
	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Invalid design file format. Please verify this is a valid .pen file."));
		return false;
	}
	return ParseDocumentObject(JsonObj, OutDocument);
}

bool FPenParser::ParseDocumentObject(const TSharedPtr<FJsonObject>& JsonObject, FDesignDocument& OutDocument)
{
	return ParseDocumentInternal(JsonObject, OutDocument);
}

namespace
{
bool ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FDesignDocument& OutDocument)
{
	if (!JsonObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Design file content is empty or malformed."));
		return false;
	}

	if (!JsonObj->TryGetStringField(TEXT("version"), OutDocument.Version) || OutDocument.Version.IsEmpty())
	{
		OutDocument.Version = TEXT("1.0");
		UE_LOG(LogDesign2UMG, Warning, TEXT("Design file is missing a version; defaulting to %s."), *OutDocument.Version);
	}

	// Document-level provider marker: component identity extraction and component matching use it to distinguish the source.
	OutDocument.Metadata.FindOrAdd(TEXT("source.provider")) = DesignComponentMapping::ProviderPencil;

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
			OutDocument.Themes.Add(Pair.Key, Values);
		}
	}

	// Parse variables.
	const TSharedPtr<FJsonValue>& VarsField = JsonObj->TryGetField(TEXT("variables"));
	if (VarsField.IsValid() && VarsField->Type == EJson::Object)
	{
		ParseVariables(VarsField->AsObject(), OutDocument.Variables);
	}

	// Alias-qualified variable refs ("$Y:--primary") resolve against the bundled
	// library variables named by the document's imports map.
	const TSharedPtr<FJsonValue>& ImportsField = JsonObj->TryGetField(TEXT("imports"));
	if (ImportsField.IsValid() && ImportsField->Type == EJson::Object)
	{
		ExpandImportedLibraryVariables(ImportsField->AsObject(), OutDocument.Variables);
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

	AppendParsedChildrenAtDepth(JsonObj->TryGetField(TEXT("children")), Resolver, OutDocument.Children, ParseContext, 0);

	if (ParseContext.bDepthExceeded)
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("Design file parsing failed because node nesting exceeds the supported depth."));
		return false;
	}

	return true;
}

void ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FDesignVariable>& OutVariables)
{
	for (const auto& Pair : JsonObj->Values)
	{
		FDesignVariable Var;
		Var.Name = Pair.Key;

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
							TV.ThemeMatch.Add(TP.Key, TP.Value->AsString());
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

void ExpandImportedLibraryVariables(const TSharedPtr<FJsonObject>& ImportsObj, TMap<FString, FDesignVariable>& InOutVariables)
{
	for (const auto& Pair : ImportsObj->Values)
	{
		if (Pair.Value->Type != EJson::String)
		{
			continue;
		}

		FString LibraryName;
		if (!ParsePencilLibraryId(Pair.Value->AsString(), LibraryName))
		{
			continue;
		}

		FString VariablesJson;
		if (!PenBundledLibraries::TryGetVariablesJson(LibraryName, VariablesJson))
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Import '%s' names library '%s' with no registered variables table."), *Pair.Value->AsString(), *LibraryName);
			continue;
		}

		TSharedPtr<FJsonObject> VariablesObj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(VariablesJson);
		if (!FJsonSerializer::Deserialize(Reader, VariablesObj) || !VariablesObj.IsValid())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Variables table for library '%s' could not be parsed."), *LibraryName);
			continue;
		}

		TMap<FString, FDesignVariable> LibraryVariables;
		ParseVariables(VariablesObj, LibraryVariables);
		for (auto& VarPair : LibraryVariables)
		{
			const FString QualifiedName = Pair.Key + TEXT(":") + VarPair.Key;
			if (!InOutVariables.Contains(QualifiedName))
			{
				VarPair.Value.SourceAlias = Pair.Key;
				InOutVariables.Add(QualifiedName, MoveTemp(VarPair.Value));
			}
		}
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
				Node->Theme.Add(Pair.Key, Pair.Value->AsString());
		}
		if (Node->Theme.Num() > 0)
		{
			ThemedResolver = Resolver;
			ThemedResolver.PushThemeOverride(Node->Theme);
			ActiveResolver = &ThemedResolver;
		}
	}

	const TSharedPtr<FJsonValue>& SizeField = JsonObj->TryGetField(TEXT("size"));
	if (SizeField.IsValid() && SizeField->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>& SizeObject = SizeField->AsObject();
		float SizeWidth = 0.0f;
		if (TryGetResolvedNumberField(SizeObject, TEXT("width"), *ActiveResolver, SizeWidth))
		{
			Node->Transform.SetWidthValue(FDesignSize::Fixed(SizeWidth));
		}

		float SizeHeight = 0.0f;
		if (TryGetResolvedNumberField(SizeObject, TEXT("height"), *ActiveResolver, SizeHeight))
		{
			Node->Transform.SetHeightValue(FDesignSize::Fixed(SizeHeight));
		}
	}

	PenParser::ApplyNodeBoxProperties(JsonObj, *Node, Node->Type, *ActiveResolver);
	TryGetResolvedNumberField(JsonObj, TEXT("rotation"), *ActiveResolver, Node->Transform.Rotation);

	// Visibility.
	TryGetResolvedBoolField(JsonObj, TEXT("enabled"), *ActiveResolver, Node->Transform.bEnabled);
	TryGetResolvedBoolField(JsonObj, TEXT("flipX"), *ActiveResolver, Node->Transform.bFlipX);
	TryGetResolvedBoolField(JsonObj, TEXT("flipY"), *ActiveResolver, Node->Transform.bFlipY);

	// Default layout: frame falls back to Horizontal, group to None.
	{
		FString LayoutStr;
		if (JsonObj->TryGetStringField(TEXT("layout"), LayoutStr))
			Node->LayoutData.Layout = PenParser::ParseLayout(LayoutStr);
		else if (Node->Type == EDesignNodeType::Group)
			Node->LayoutData.Layout = EDesignLayout::None;
	}
	PenParser::ApplyNodeLayoutProperties(JsonObj, *Node, *ActiveResolver, false);
	if (JsonObj->HasField(TEXT("layoutIncludeStroke")))
	{
		bool bLayoutIncludeStroke = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("layoutIncludeStroke"), *ActiveResolver, bLayoutIncludeStroke))
		{
			Node->Metadata.Add(TEXT("layoutIncludeStroke"), bLayoutIncludeStroke ? TEXT("true") : TEXT("false"));
		}
	}
	TryGetResolvedNumberField(JsonObj, TEXT("layoutGrow"), *ActiveResolver, Node->LayoutData.LayoutGrow);

	// Visual properties.
	if (JsonObj->HasField(TEXT("fill")))
		PenParser::ParseNodeFillProperties(JsonObj->TryGetField(TEXT("fill")), *Node, *ActiveResolver);
	PenParser::ParseNodeStrokeProperties(JsonObj, *Node, *ActiveResolver);
	if (JsonObj->HasField(TEXT("cornerRadius")))
		Node->Visual.CornerRadius = ActiveResolver->ResolveCornerRadius(JsonObj->TryGetField(TEXT("cornerRadius")));

	PenParserNodeExtensions::ParseNodeExtended(JsonObj, Node, *ActiveResolver);

	AppendParsedChildrenAtDepth(JsonObj->TryGetField(TEXT("children")), *ActiveResolver, Node->Children, ParseContext, Depth + 1);
	PenParserNodeExtensions::FinalizePenPaddingRuntimeWrapPolicy(Node);

	PenParserNodeExtensions::ParsePenSemanticMetadata(JsonObj, *Node, *ActiveResolver);

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
			OutChildren.Add(Child);
	}
}
}

namespace PenParser
{
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
