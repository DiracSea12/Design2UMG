// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParser.h"
#include "Design2UMGLog.h"
#include "DesignJsonUtils.h"
#include "DesignImportTypes.h"
#include "DesignSchema.h"
#include "DesignVariableResolver.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"
#include "PenParserInternal.h"
#include "PenParserNodeExtensions.h"

namespace
{
	constexpr int32 MaxPenParserNodeDepth = 512;

	struct FPenParserParseContext
	{
		bool bDepthExceeded = false;
	};

	void ApplyPenStrokeExtensions(
		const TSharedPtr<FJsonObject>& StrokeObj,
		FDesignNode& Node,
		const FDesignVariableResolver& Resolver)
	{
		if (!StrokeObj.IsValid())
			return;

		FString Join;
		if (StrokeObj->TryGetStringField(TEXT("join"), Join) && !Join.IsEmpty())
			Node.Metadata.Add(TEXT("stroke.join"), Join);

		double MiterAngle = 0.0;
		if (TryGetResolvedNumberField(StrokeObj, TEXT("miterAngle"), Resolver, MiterAngle))
			Node.Metadata.Add(TEXT("stroke.miterAngle"), FString::SanitizeFloat(static_cast<float>(MiterAngle)));

		FString Cap;
		if (StrokeObj->TryGetStringField(TEXT("cap"), Cap) && !Cap.IsEmpty())
			Node.Metadata.Add(TEXT("stroke.cap"), Cap);

		const TSharedPtr<FJsonValue> DashPatternField = StrokeObj->TryGetField(TEXT("dashPattern"));
		if (DashPatternField.IsValid() && DashPatternField->Type == EJson::Array)
			Node.Metadata.Add(TEXT("stroke.dashPattern"), DesignJsonUtils::ToCompactString(DashPatternField));
	}

	bool TryParseMeshHandle(
		const TSharedPtr<FJsonObject>& PointObj,
		const FDesignVariableResolver& Resolver,
		const FString& PrimaryField,
		const FString& FallbackField,
		FDesignMeshGradientHandle& OutHandle)
	{
		if (!PointObj.IsValid())
			return false;

		const TSharedPtr<FJsonValue> Primary = PointObj->TryGetField(PrimaryField);
		const TSharedPtr<FJsonValue> Fallback = PointObj->TryGetField(FallbackField);
		const TSharedPtr<FJsonValue> HandleField = Primary.IsValid() ? Primary : Fallback;
		if (!HandleField.IsValid())
			return false;

		float X = 0.0f;
		float Y = 0.0f;
		if (!TryParsePointXY(HandleField, Resolver, X, Y))
			return false;

		OutHandle.Value = FVector2D(X, Y);
		return true;
	}

	const PenParserNodeExtensions::FParseCallbacks& GetNodeExtensionCallbacks();
}

bool ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FDesignDocument& OutDocument);
void ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FDesignVariable>& OutVariables);
bool ValidateNodeDepth(const TSharedPtr<FJsonObject>& JsonObj, FPenParserParseContext& ParseContext, int32 Depth);
bool ValidateChildrenDepth(const TSharedPtr<FJsonValue>& ChildrenField, FPenParserParseContext& ParseContext, int32 Depth);
TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver);
TSharedPtr<FDesignNode> ParseNodeAtDepth(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver, FPenParserParseContext& ParseContext, int32 Depth);
void AppendParsedChildren(const TSharedPtr<FJsonValue>& ChildrenField, const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& OutChildren);
void AppendParsedChildrenAtDepth(const TSharedPtr<FJsonValue>& ChildrenField, const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& OutChildren, FPenParserParseContext& ParseContext, int32 Depth);
void ParseNodeFillProperties(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver);
void ParseNodeStrokeProperties(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver);
void ApplyNodeBoxProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, EDesignNodeType SizeNodeType, const FDesignVariableResolver& Resolver);
void ApplyNodeLayoutProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver, bool bIncludeTextProperties);
void ApplyNodeTextStyleProperties(const TSharedPtr<FJsonObject>& JsonObj, FDesignNode& OutNode, const FDesignVariableResolver& Resolver);
FDesignSize ParseSize(const TSharedPtr<FJsonValue>& Value, EDesignNodeType NodeType, const FDesignVariableResolver& Resolver);
FDesignFill ParseFill(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);
FDesignStroke ParseStroke(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver);
FDesignPadding ParsePadding(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver);
EDesignNodeType ParseNodeType(const FString& S);
EDesignLayout ParseLayout(const FString& S);
EDesignJustifyContent ParseJustifyContent(const FString& S);
EDesignAlignItems ParseAlignItems(const FString& S);
EDesignTextAlign ParseTextAlign(const FString& S);
EDesignTextAlignVertical ParseTextAlignVertical(const FString& S);
EDesignTextGrowth ParseTextGrowth(const FString& S);

namespace
{
	const PenParserNodeExtensions::FParseCallbacks& GetNodeExtensionCallbacks()
	{
		static const PenParserNodeExtensions::FParseCallbacks Callbacks{
			&ParseNodeFillProperties,
			&ParseNodeStrokeProperties,
			&ApplyNodeBoxProperties,
			&ApplyNodeLayoutProperties,
			&ApplyNodeTextStyleProperties,
			&ParseTextAlign,
			&ParseTextAlignVertical,
			&ParseTextGrowth,
			&ParseNode,
			&AppendParsedChildren
		};
		return Callbacks;
	}
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
			const TArray<TSharedPtr<FJsonValue>>& Arr = Pair.Value->AsArray();
			for (const auto& Val : Arr)
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

	// Build variable resolver.
	FDesignVariableResolver Resolver(OutDocument.Variables);

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
			const TArray<TSharedPtr<FJsonValue>>& Arr = ValueField->AsArray();
			for (int32 i = 0; i < Arr.Num(); ++i)
			{
				const TSharedPtr<FJsonObject>& EntryObj = Arr[i]->AsObject();
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

bool ValidateNodeDepth(
	const TSharedPtr<FJsonObject>& JsonObj,
	FPenParserParseContext& ParseContext,
	int32 Depth)
{
	if (!JsonObj.IsValid())
		return true;

	if (Depth > MaxPenParserNodeDepth)
	{
		ParseContext.bDepthExceeded = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document children nesting is too deep (over %d levels)."), MaxPenParserNodeDepth);
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

	if (Depth > MaxPenParserNodeDepth)
	{
		ParseContext.bDepthExceeded = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document children nesting is too deep (over %d levels)."), MaxPenParserNodeDepth);
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

TSharedPtr<FDesignNode> ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver)
{
	FPenParserParseContext ParseContext;
	return ParseNodeAtDepth(JsonObj, Resolver, ParseContext, 0);
}

TSharedPtr<FDesignNode> ParseNodeAtDepth(
	const TSharedPtr<FJsonObject>& JsonObj,
	const FDesignVariableResolver& Resolver,
	FPenParserParseContext& ParseContext,
	int32 Depth)
{
	if (!JsonObj.IsValid())
		return nullptr;

	if (Depth > MaxPenParserNodeDepth)
	{
		ParseContext.bDepthExceeded = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document nesting is too deep (over %d levels); node skipped."), MaxPenParserNodeDepth);
		return nullptr;
	}

    TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
    Node->Metadata.Add(TEXT("source.provider"), TEXT("pen"));

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
	Node->Type = ParseNodeType(TypeStr);

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
			ThemedResolver.SetThemeContext(Node->Theme);
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

	ApplyNodeBoxProperties(JsonObj, *Node, Node->Type, *ActiveResolver);
	TryGetResolvedNumberField(JsonObj, TEXT("rotation"), *ActiveResolver, Node->Transform.Rotation);

	// Visibility.
	TryGetResolvedBoolField(JsonObj, TEXT("enabled"), *ActiveResolver, Node->Transform.bEnabled);
	TryGetResolvedBoolField(JsonObj, TEXT("flipX"), *ActiveResolver, Node->Transform.bFlipX);
	TryGetResolvedBoolField(JsonObj, TEXT("flipY"), *ActiveResolver, Node->Transform.bFlipY);

	// Default layout: frame falls back to Horizontal, group to None.
	{
		FString LayoutStr;
		if (JsonObj->TryGetStringField(TEXT("layout"), LayoutStr))
			Node->LayoutData.Layout = ParseLayout(LayoutStr);
		else if (Node->Type == EDesignNodeType::Group)
			Node->LayoutData.Layout = EDesignLayout::None;
	}
	ApplyNodeLayoutProperties(JsonObj, *Node, *ActiveResolver, false);
	if (JsonObj->HasField(TEXT("layoutIncludeStroke")))
	{
		bool bLayoutIncludeStroke = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("layoutIncludeStroke"), *ActiveResolver, bLayoutIncludeStroke))
		{
			// Typed source extensions are the primary storage; keep the Pen key as an explicit source-shaped alias.
			Node->MutableSource().LayoutExtensions.StrokesIncludedInLayout = bLayoutIncludeStroke;
			Node->Metadata.Add(TEXT("layoutIncludeStroke"), bLayoutIncludeStroke ? TEXT("true") : TEXT("false"));
		}
	}
	TryGetResolvedNumberField(JsonObj, TEXT("layoutGrow"), *ActiveResolver, Node->LayoutData.LayoutGrow);

	// Visual properties.
	if (JsonObj->HasField(TEXT("fill")))
		ParseNodeFillProperties(JsonObj->TryGetField(TEXT("fill")), *Node, *ActiveResolver);
	ParseNodeStrokeProperties(JsonObj->TryGetField(TEXT("stroke")), *Node, *ActiveResolver);
	if (JsonObj->HasField(TEXT("cornerRadius")))
		Node->Visual.CornerRadius = ActiveResolver->ResolveCornerRadius(JsonObj->TryGetField(TEXT("cornerRadius")));

	PenParserNodeExtensions::ParseNodeExtended(JsonObj, Node, *ActiveResolver, GetNodeExtensionCallbacks());

	AppendParsedChildrenAtDepth(JsonObj->TryGetField(TEXT("children")), *ActiveResolver, Node->Children, ParseContext, Depth + 1);
	PenParserNodeExtensions::FinalizePenPaddingRuntimeWrapPolicy(Node);

	PenParserNodeExtensions::ParsePenSemanticMetadata(JsonObj, *Node, *ActiveResolver);

	return Node;
}

void AppendParsedChildren(
	const TSharedPtr<FJsonValue>& ChildrenField,
	const FDesignVariableResolver& Resolver,
	TArray<TSharedPtr<FDesignNode>>& OutChildren)
{
	FPenParserParseContext ParseContext;
	AppendParsedChildrenAtDepth(ChildrenField, Resolver, OutChildren, ParseContext, 0);
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

	if (Depth > MaxPenParserNodeDepth)
	{
		ParseContext.bDepthExceeded = true;
		UE_LOG(LogDesign2UMG, Error, TEXT("Pen document children nesting is too deep (over %d levels); remaining children skipped."), MaxPenParserNodeDepth);
		return;
	}

	for (const TSharedPtr<FJsonValue>& ChildVal : ChildrenField->AsArray())
	{
		if (TSharedPtr<FDesignNode> Child = ParseNodeAtDepth(ChildVal->AsObject(), Resolver, ParseContext, Depth))
			OutChildren.Add(Child);
	}
}

void ParseNodeFillProperties(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver)
{
	OutNode.Visual.Fill = ParseFill(Value, Resolver);
	OutNode.Visual.Fills.Empty();
	if (Value.IsValid() && Value->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
		{
			FDesignFill Fill = ParseFill(Item, Resolver);
			if (Fill.Type != EDesignFillType::None && Fill.bEnabled)
				OutNode.Visual.Fills.Add(MoveTemp(Fill));
		}
	}
	else if (OutNode.Visual.Fill.Type != EDesignFillType::None)
	{
		OutNode.Visual.Fills.Add(OutNode.Visual.Fill);
	}
}

void ParseNodeStrokeProperties(const TSharedPtr<FJsonValue>& Value, FDesignNode& OutNode, const FDesignVariableResolver& Resolver)
{
	if (!Value.IsValid() || Value->Type != EJson::Object)
		return;

	const TSharedPtr<FJsonObject> StrokeObj = Value->AsObject();
	OutNode.Visual.Stroke = ParseStroke(StrokeObj, Resolver);
	ApplyPenStrokeExtensions(StrokeObj, OutNode, Resolver);
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





