// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PenParser.h"
#include "Design2UMGLog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/DefaultValueHelper.h"

namespace
{
	bool TryParseNumericString(const FString& InValue, double& OutValue)
	{
		const FString Trimmed = InValue.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
			return false;

        double Parsed = 0.0;
        if (!FDefaultValueHelper::ParseDouble(Trimmed, Parsed))
            return false;

		OutValue = Parsed;
		return true;
	}

	bool TryParseBooleanString(const FString& InValue, bool& OutValue)
	{
		const FString Normalized = InValue.TrimStartAndEnd().ToLower();
		if (Normalized == TEXT("true") || Normalized == TEXT("1") || Normalized == TEXT("yes") || Normalized == TEXT("on"))
		{
			OutValue = true;
			return true;
		}
		if (Normalized == TEXT("false") || Normalized == TEXT("0") || Normalized == TEXT("no") || Normalized == TEXT("off"))
		{
			OutValue = false;
			return true;
		}
		return false;
	}

	bool TryGetNumberValueFlexible(const TSharedPtr<FJsonValue>& Value, double& OutValue)
	{
		if (!Value.IsValid())
			return false;

		if (Value->Type == EJson::Number)
		{
			OutValue = Value->AsNumber();
			return true;
		}

		if (Value->Type == EJson::String)
		{
			return TryParseNumericString(Value->AsString(), OutValue);
		}

		return false;
	}

	template <typename NumberType>
	bool TryGetNumberValueFlexible(const TSharedPtr<FJsonValue>& Value, NumberType& OutValue)
	{
		double Parsed = 0.0;
		if (!TryGetNumberValueFlexible(Value, Parsed))
			return false;

		OutValue = static_cast<NumberType>(Parsed);
		return true;
	}

	template <typename NumberType>
	bool TryGetNumberFieldFlexible(
		const TSharedPtr<FJsonObject>& Obj,
		const FString& FieldName,
		NumberType& OutValue)
	{
		if (!Obj.IsValid() || !Obj->HasField(FieldName))
			return false;

		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(FieldName);
		if (!FieldValue.IsValid())
			return false;

		double Parsed = 0.0;
		if (!TryGetNumberValueFlexible(FieldValue, Parsed))
			return false;

		OutValue = static_cast<NumberType>(Parsed);
		return true;
	}

	template <typename NumberType>
	bool TryGetResolvedNumberField(
		const TSharedPtr<FJsonObject>& Obj,
		const FString& FieldName,
		const FDesignVariableResolver& Resolver,
		NumberType& OutValue)
	{
		if (!Obj.IsValid() || !Obj->HasField(FieldName))
			return false;

		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(FieldName);
		if (!FieldValue.IsValid())
			return false;

		if (TryGetNumberValueFlexible(FieldValue, OutValue))
			return true;

		if (FieldValue->Type != EJson::String)
			return false;

		const FString RawValue = FieldValue->AsString();
		if (!FDesignVariableResolver::IsVariableRef(RawValue))
			return false;

		OutValue = static_cast<NumberType>(Resolver.ResolveNumber(RawValue));
		return true;
	}

	bool TryGetResolvedBoolField(
		const TSharedPtr<FJsonObject>& Obj,
		const FString& FieldName,
		const FDesignVariableResolver& Resolver,
		bool& OutValue)
	{
		if (!Obj.IsValid() || !Obj->HasField(FieldName))
			return false;

		const TSharedPtr<FJsonValue> FieldValue = Obj->TryGetField(FieldName);
		if (!FieldValue.IsValid())
			return false;

		if (FieldValue->Type == EJson::Boolean)
		{
			OutValue = FieldValue->AsBool();
			return true;
		}

		if (FieldValue->Type != EJson::String)
			return false;

		const FString RawValue = FieldValue->AsString();
		if (TryParseBooleanString(RawValue, OutValue))
			return true;

		if (!FDesignVariableResolver::IsVariableRef(RawValue))
			return false;

		return TryParseBooleanString(Resolver.ResolveString(RawValue), OutValue);
	}

	FString SerializeJsonValueCompact(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
			return FString();

		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		if (!FJsonSerializer::Serialize(Value.ToSharedRef(), FString(), Writer))
			return FString();
		return Output;
	}

	void ApplyPenSemanticHints(FDesignNode& Node, const bool bPenPlaceholder, const bool bPenHasSlot, const int32 PenSlotNodeCount)
	{
		const bool bNameLooksLikePlaceholder = Node.Name.Contains(TEXT("placeholder"), ESearchCase::IgnoreCase);
		const bool bNameLooksLikeSlot = Node.Name.Contains(TEXT("slot"), ESearchCase::IgnoreCase);
		const bool bNameLooksLikeContentSlot = bPenHasSlot && Node.Name.Contains(TEXT("content"), ESearchCase::IgnoreCase);
		Node.bSemanticPlaceholderSurface =
			bPenPlaceholder
			|| bPenHasSlot
			|| PenSlotNodeCount > 0
			|| bNameLooksLikePlaceholder
			|| bNameLooksLikeContentSlot
			|| bNameLooksLikeSlot;
	}

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
			Node.Metadata.Add(TEXT("stroke.dashPattern"), SerializeJsonValueCompact(DashPatternField));
	}

	bool TryParsePointXY(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver, float& OutX, float& OutY)
	{
		if (!Value.IsValid())
			return false;

		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (!Obj.IsValid())
				return false;
			const bool bHasX = TryGetResolvedNumberField(Obj, TEXT("x"), Resolver, OutX);
			const bool bHasY = TryGetResolvedNumberField(Obj, TEXT("y"), Resolver, OutY);
			return bHasX && bHasY;
		}

		if (Value->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Arr = Value->AsArray();
			if (Arr.Num() < 2)
				return false;
			return TryGetNumberValueFlexible(Arr[0], OutX) && TryGetNumberValueFlexible(Arr[1], OutY);
		}

		return false;
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

		OutHandle.X = X;
		OutHandle.Y = Y;
		OutHandle.bHasValue = true;
		return true;
	}

	void ParseTextRunFromObject(
		const TSharedPtr<FJsonObject>& SegmentObj,
		const FDesignVariableResolver& Resolver,
		FDesignTextRun& OutRun)
	{
		if (!SegmentObj.IsValid())
			return;

		FString SegmentText;
		if (!SegmentObj->TryGetStringField(TEXT("content"), SegmentText))
			SegmentObj->TryGetStringField(TEXT("text"), SegmentText);
		OutRun.Content = Resolver.ResolveString(SegmentText);

		TSharedPtr<FJsonObject> StyleObj = SegmentObj;
		const TSharedPtr<FJsonObject>* NestedStyle = nullptr;
		if (SegmentObj->TryGetObjectField(TEXT("style"), NestedStyle) && NestedStyle && NestedStyle->IsValid())
			StyleObj = *NestedStyle;
		else if (SegmentObj->TryGetObjectField(TEXT("textStyle"), NestedStyle) && NestedStyle && NestedStyle->IsValid())
			StyleObj = *NestedStyle;

		FString FontFamily;
		if (StyleObj->TryGetStringField(TEXT("fontFamily"), FontFamily))
			OutRun.FontFamily = Resolver.ResolveString(FontFamily);

		OutRun.bHasFontSize = TryGetResolvedNumberField(StyleObj, TEXT("fontSize"), Resolver, OutRun.FontSize);
		StyleObj->TryGetStringField(TEXT("fontWeight"), OutRun.FontWeight);
		StyleObj->TryGetStringField(TEXT("fontStyle"), OutRun.FontStyle);
		OutRun.bHasLineHeight = TryGetResolvedNumberField(StyleObj, TEXT("lineHeight"), Resolver, OutRun.LineHeight);
		OutRun.bHasLetterSpacing = TryGetResolvedNumberField(StyleObj, TEXT("letterSpacing"), Resolver, OutRun.LetterSpacing);
		OutRun.bHasUnderline = TryGetResolvedBoolField(StyleObj, TEXT("underline"), Resolver, OutRun.bUnderline);
		OutRun.bHasStrikethrough = TryGetResolvedBoolField(StyleObj, TEXT("strikethrough"), Resolver, OutRun.bStrikethrough);
		StyleObj->TryGetStringField(TEXT("href"), OutRun.Href);
	}
}

bool FPenParser::ParseFromFile(const FString& FilePath, FDesignDocument& OutDocument)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("PenParser: Failed to load file: %s"), *FilePath);
		UE_LOG(LogDesign2UMG, Error, TEXT("鏃犳硶璇诲彇璁捐鏂囦欢銆傝纭鏂囦欢璺緞鏈夋晥涓旀枃浠舵湭琚崰鐢? %s"), *FilePath);
		return false;
	}
	return ParseDocument(JsonString, OutDocument);
}

bool FPenParser::ParseDocument(const FString& JsonString, FDesignDocument& OutDocument)
{
	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("PenParser: Failed to parse JSON"));
		UE_LOG(LogDesign2UMG, Error, TEXT("Invalid design file format. Please verify this is a valid .pen file."));
		return false;
	}
	return ParseDocumentInternal(JsonObj, OutDocument);
}

bool FPenParser::ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FDesignDocument& OutDocument)
{
	if (!JsonObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("PenParser: Invalid root json object"));
		UE_LOG(LogDesign2UMG, Error, TEXT("Design file content is empty or malformed."));
		return false;
	}

	if (!JsonObj->TryGetStringField(TEXT("version"), OutDocument.Version) || OutDocument.Version.IsEmpty())
	{
		OutDocument.Version = TEXT("1.0");
		UE_LOG(LogDesign2UMG, Verbose, TEXT("PenParser: missing 'version', fallback to %s"), *OutDocument.Version);
		UE_LOG(LogDesign2UMG, Warning, TEXT("璁捐鏂囦欢缂哄皯鐗堟湰鍙凤紝浣跨敤榛樿鐗堟湰 %s"), *OutDocument.Version);
	}

	// 瑙ｆ瀽瀛椾綋
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

	// 瑙ｆ瀽涓婚
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

	// 瑙ｆ瀽鍙橀噺
	const TSharedPtr<FJsonValue>& VarsField = JsonObj->TryGetField(TEXT("variables"));
	if (VarsField.IsValid() && VarsField->Type == EJson::Object)
	{
		ParseVariables(VarsField->AsObject(), OutDocument.Variables);
	}

	// Build variable resolver.
	FDesignVariableResolver Resolver(OutDocument.Variables);

	// Parse top-level children.
	const TSharedPtr<FJsonValue>& ChildrenField = JsonObj->TryGetField(TEXT("children"));
	if (ChildrenField.IsValid() && ChildrenField->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>& ChildrenArr = ChildrenField->AsArray();
		for (const auto& ChildVal : ChildrenArr)
		{
			// Read top-level child node.
			const TSharedPtr<FJsonObject>& ChildObj = ChildVal->AsObject();
			if (ChildObj.IsValid())
			{
				float FrameX = 0.0f, FrameY = 0.0f;
				TryGetResolvedNumberField(ChildObj, TEXT("x"), Resolver, FrameX);
				TryGetResolvedNumberField(ChildObj, TEXT("y"), Resolver, FrameY);
				if (auto Node = ParseNode(ChildObj, Resolver, FrameX, FrameY))
					OutDocument.Children.Add(Node);
			}
		}
	}

	return true;
}

void FPenParser::ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FDesignVariable>& OutVariables)
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

		// 瑙ｆ瀽鍊硷紙鍙兘鏄崟鍊兼垨涓婚鍊兼暟缁勶級
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

TSharedPtr<FDesignNode> FPenParser::ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver, float ParentX, float ParentY)
{
	(void)ParentX;
	(void)ParentY;

	if (!JsonObj.IsValid())
		return nullptr;

	TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();

	// Basic node fields.
	if (!JsonObj->TryGetStringField(TEXT("id"), Node->Id))
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("PenParser: Node missing 'id' field, skipping"));
		UE_LOG(LogDesign2UMG, Warning, TEXT("Design file contains a node without id; skipped."));
		return nullptr;
	}
	JsonObj->TryGetStringField(TEXT("name"), Node->Name);
	FString TypeStr;
	if (!JsonObj->TryGetStringField(TEXT("type"), TypeStr))
	{
		UE_LOG(LogDesign2UMG, Verbose, TEXT("PenParser: Node '%s' missing 'type' field, skipping"), *Node->Id);
		UE_LOG(LogDesign2UMG, Warning, TEXT("鑺傜偣 '%s' 缂哄皯绫诲瀷淇℃伅锛屽凡璺宠繃"), *Node->Id);
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

	// Position - .pen uses parent-local coordinates (not document-absolute).
	// Keep raw x/y as-is to avoid double-subtracting parent offsets.
	float AbsoluteX = 0.0f, AbsoluteY = 0.0f;
	if (JsonObj->HasField(TEXT("x")))
	{
		if (TryGetResolvedNumberField(JsonObj, TEXT("x"), *ActiveResolver, AbsoluteX))
		{
			Node->X = AbsoluteX;
			Node->bHasX = true;
		}
	}
	if (JsonObj->HasField(TEXT("y")))
	{
		if (TryGetResolvedNumberField(JsonObj, TEXT("y"), *ActiveResolver, AbsoluteY))
		{
			Node->Y = AbsoluteY;
			Node->bHasY = true;
		}
	}
	TryGetResolvedNumberField(JsonObj, TEXT("rotation"), *ActiveResolver, Node->Rotation);
	const TSharedPtr<FJsonValue>& SizeField = JsonObj->TryGetField(TEXT("size"));
	if (SizeField.IsValid() && SizeField->Type == EJson::Object)
	{
		const auto& SizeObj = SizeField->AsObject();
		float SizeWidth = 0.0f;
		if (TryGetResolvedNumberField(SizeObj, TEXT("width"), *ActiveResolver, SizeWidth))
		{
			Node->Width = FDesignSize::Fixed(SizeWidth);
			Node->bHasWidth = true;
		}
		float SizeHeight = 0.0f;
		if (TryGetResolvedNumberField(SizeObj, TEXT("height"), *ActiveResolver, SizeHeight))
		{
			Node->Height = FDesignSize::Fixed(SizeHeight);
			Node->bHasHeight = true;
		}
	}

	// Top-level width/height have higher priority.
	if (JsonObj->HasField(TEXT("width")))
	{
		Node->Width = ParseSize(JsonObj->TryGetField(TEXT("width")), Node->Type, *ActiveResolver);
		Node->bHasWidth = true;
	}
	if (JsonObj->HasField(TEXT("height")))
	{
		Node->Height = ParseSize(JsonObj->TryGetField(TEXT("height")), Node->Type, *ActiveResolver);
		Node->bHasHeight = true;
	}

	// Visibility.
	TryGetResolvedBoolField(JsonObj, TEXT("enabled"), *ActiveResolver, Node->bEnabled);
	if (JsonObj->HasField(TEXT("opacity")))
		Node->bHasOpacity = TryGetResolvedNumberField(JsonObj, TEXT("opacity"), *ActiveResolver, Node->Opacity);
	TryGetResolvedBoolField(JsonObj, TEXT("flipX"), *ActiveResolver, Node->bFlipX);
	TryGetResolvedBoolField(JsonObj, TEXT("flipY"), *ActiveResolver, Node->bFlipY);

	// 甯冨眬灞炴€?- Frame 榛樿 Horizontal锛孏roup 榛樿 None
	{
		FString LayoutStr;
		if (JsonObj->TryGetStringField(TEXT("layout"), LayoutStr))
			Node->Layout = ParseLayout(LayoutStr);
		else if (Node->Type == EDesignNodeType::Group)
			Node->Layout = EDesignLayout::None;
	}
	if (JsonObj->HasField(TEXT("gap")))
		Node->bHasGap = TryGetResolvedNumberField(JsonObj, TEXT("gap"), *ActiveResolver, Node->Gap);
	if (JsonObj->HasField(TEXT("padding")))
	{
		Node->Padding = ParsePadding(JsonObj->TryGetField(TEXT("padding")), *ActiveResolver);
		Node->bHasPadding = true;
	}
	{
		FString JCStr;
		if (JsonObj->TryGetStringField(TEXT("justifyContent"), JCStr))
			Node->JustifyContent = ParseJustifyContent(JCStr);
	}
	{
		FString AIStr;
		if (JsonObj->TryGetStringField(TEXT("alignItems"), AIStr))
			Node->AlignItems = ParseAlignItems(AIStr);
	}
	if (JsonObj->HasField(TEXT("clip")))
		Node->bHasClip = TryGetResolvedBoolField(JsonObj, TEXT("clip"), *ActiveResolver, Node->bClip);
	if (JsonObj->HasField(TEXT("layoutIncludeStroke")))
	{
		bool bLayoutIncludeStroke = false;
		if (TryGetResolvedBoolField(JsonObj, TEXT("layoutIncludeStroke"), *ActiveResolver, bLayoutIncludeStroke))
			Node->Metadata.Add(TEXT("layoutIncludeStroke"), bLayoutIncludeStroke ? TEXT("true") : TEXT("false"));
	}
	bool bPenPlaceholder = false;
	TryGetResolvedBoolField(JsonObj, TEXT("placeholder"), *ActiveResolver, bPenPlaceholder);
	bool bPenHasSlot = false;
	int32 PenSlotNodeCount = 0;
	const TSharedPtr<FJsonValue>& SlotField = JsonObj->TryGetField(TEXT("slot"));
	if (SlotField.IsValid() && SlotField->Type == EJson::Array)
	{
		bPenHasSlot = true;
		for (const TSharedPtr<FJsonValue>& SlotVal : SlotField->AsArray())
		{
			if (!SlotVal.IsValid())
				continue;
			FString SlotId;
			if (SlotVal->TryGetString(SlotId))
				++PenSlotNodeCount;
		}
	}
	if (bPenHasSlot)
	{
		Node->Metadata.Add(TEXT("pen.slot.present"), TEXT("true"));
		Node->Metadata.Add(TEXT("pen.slot.count"), FString::FromInt(PenSlotNodeCount));
		if (PenSlotNodeCount == 0)
			Node->Metadata.Add(TEXT("pen.slot.empty"), TEXT("true"));
	}
	TryGetResolvedNumberField(JsonObj, TEXT("layoutGrow"), *ActiveResolver, Node->LayoutGrow);

	// Visual properties.
	if (JsonObj->HasField(TEXT("fill")))
	{
		const TSharedPtr<FJsonValue>& FillField = JsonObj->TryGetField(TEXT("fill"));
		Node->Fill = ParseFill(FillField, *ActiveResolver);
		// 澶氬眰濉厖锛歠ill 涓洪潪绌烘暟缁勬椂锛岄€愪釜瑙ｆ瀽鍒?Fills
		if (FillField.IsValid() && FillField->Type == EJson::Array)
		{
			const auto& Arr = FillField->AsArray();
			for (const auto& Item : Arr)
			{
				FDesignFill F = ParseFill(Item, *ActiveResolver);
				if (F.Type != EDesignFillType::None && F.bEnabled)
					Node->Fills.Add(MoveTemp(F));
			}
		}
		else if (Node->Fill.Type != EDesignFillType::None)
		{
			Node->Fills.Add(Node->Fill);
		}
	}
	{
		const TSharedPtr<FJsonValue>& StrokeField = JsonObj->TryGetField(TEXT("stroke"));
		if (StrokeField.IsValid() && StrokeField->Type == EJson::Object)
		{
			Node->Stroke = ParseStroke(StrokeField->AsObject(), *ActiveResolver);
			ApplyPenStrokeExtensions(StrokeField->AsObject(), *Node, *ActiveResolver);
		}
	}
	if (JsonObj->HasField(TEXT("cornerRadius")))
		Node->CornerRadius = ActiveResolver->ResolveCornerRadius(JsonObj->TryGetField(TEXT("cornerRadius")));

	// 缁х画瑙ｆ瀽鍏朵粬灞炴€?..
	ParseNodeExtended(JsonObj, Node, *ActiveResolver);

	// 瀛愯妭鐐癸紙缁ф壙涓婚涓婁笅鏂囷級
	const TSharedPtr<FJsonValue>& NodeChildrenField = JsonObj->TryGetField(TEXT("children"));
	if (NodeChildrenField.IsValid() && NodeChildrenField->Type == EJson::Array)
	{
		const auto& ChildrenArr = NodeChildrenField->AsArray();
		for (const auto& ChildVal : ChildrenArr)
		{
			// .pen uses local coordinates, no parent absolute transform is needed.
			if (auto Child = ParseNode(ChildVal->AsObject(), *ActiveResolver, AbsoluteX, AbsoluteY))
				Node->Children.Add(Child);
		}
	}

	ApplyPenSemanticHints(*Node, bPenPlaceholder, bPenHasSlot, PenSlotNodeCount);

	return Node;
}

void FPenParser::ParseNodeExtended(const TSharedPtr<FJsonObject>& JsonObj, TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver)
{
	// Text properties.
	const TSharedPtr<FJsonValue>& ContentField = JsonObj->TryGetField(TEXT("content"));
	if (ContentField.IsValid())
	{
		if (ContentField->Type == EJson::String)
		{
			Node->Content = Resolver.ResolveString(ContentField->AsString());
		}
	else if (ContentField->Type == EJson::Array)
	{
		Node->Metadata.Add(TEXT("text.content.raw"), SerializeJsonValueCompact(ContentField));
		FString CombinedContent;
		Node->TextRuns.Reset();
		for (const TSharedPtr<FJsonValue>& SegmentVal : ContentField->AsArray())
		{
			if (!SegmentVal.IsValid())
				continue;

			if (SegmentVal->Type == EJson::String)
			{
				FDesignTextRun SegmentRun;
				SegmentRun.Content = Resolver.ResolveString(SegmentVal->AsString());
				SegmentRun.RawJson = SerializeJsonValueCompact(SegmentVal);
				CombinedContent += SegmentRun.Content;
				Node->TextRuns.Add(MoveTemp(SegmentRun));
				continue;
			}

			if (SegmentVal->Type != EJson::Object)
				continue;

			const TSharedPtr<FJsonObject>& SegmentObj = SegmentVal->AsObject();
			if (!SegmentObj.IsValid())
				continue;

			FDesignTextRun SegmentRun;
			ParseTextRunFromObject(SegmentObj, Resolver, SegmentRun);
			SegmentRun.RawJson = SerializeJsonValueCompact(SegmentVal);
			if (!SegmentRun.Content.IsEmpty())
				CombinedContent += SegmentRun.Content;
			Node->TextRuns.Add(MoveTemp(SegmentRun));
		}
		Node->Content = CombinedContent;
	}
}
	{
		FString RawFontFamily;
		if (JsonObj->TryGetStringField(TEXT("fontFamily"), RawFontFamily))
			Node->FontFamily = Resolver.ResolveString(RawFontFamily);
	}
	TryGetResolvedNumberField(JsonObj, TEXT("fontSize"), Resolver, Node->FontSize);
	JsonObj->TryGetStringField(TEXT("fontWeight"), Node->FontWeight);
	JsonObj->TryGetStringField(TEXT("fontStyle"), Node->FontStyle);
	TryGetResolvedNumberField(JsonObj, TEXT("lineHeight"), Resolver, Node->LineHeight);
	TryGetResolvedNumberField(JsonObj, TEXT("letterSpacing"), Resolver, Node->LetterSpacing);
	TryGetResolvedBoolField(JsonObj, TEXT("underline"), Resolver, Node->bUnderline);
	TryGetResolvedBoolField(JsonObj, TEXT("strikethrough"), Resolver, Node->bStrikethrough);
	JsonObj->TryGetStringField(TEXT("href"), Node->Href);

	{
		FString TAStr;
		if (JsonObj->TryGetStringField(TEXT("textAlign"), TAStr))
			Node->TextAlign = ParseTextAlign(TAStr);
	}
	{
		FString TAVStr;
		if (JsonObj->TryGetStringField(TEXT("textAlignVertical"), TAVStr))
			Node->TextAlignVertical = ParseTextAlignVertical(TAVStr);
	}
	{
		FString TGStr;
		if (JsonObj->TryGetStringField(TEXT("textGrowth"), TGStr))
			Node->TextGrowth = ParseTextGrowth(TGStr);
	}

	// Icon font properties.
	JsonObj->TryGetStringField(TEXT("iconFontName"), Node->IconFontName);
	{
		FString RawIconFontFamily;
		if (JsonObj->TryGetStringField(TEXT("iconFontFamily"), RawIconFontFamily))
			Node->IconFontFamily = Resolver.ResolveString(RawIconFontFamily);
	}
	TryGetResolvedNumberField(JsonObj, TEXT("weight"), Resolver, Node->IconWeight);
	TryGetResolvedNumberField(JsonObj, TEXT("innerRadius"), Resolver, Node->InnerRadius);
	TryGetResolvedNumberField(JsonObj, TEXT("startAngle"), Resolver, Node->StartAngle);
	TryGetResolvedNumberField(JsonObj, TEXT("sweepAngle"), Resolver, Node->SweepAngle);
	TryGetResolvedNumberField(JsonObj, TEXT("polygonCount"), Resolver, Node->PolygonCount);
	JsonObj->TryGetStringField(TEXT("geometry"), Node->Geometry);
	JsonObj->TryGetStringField(TEXT("fillRule"), Node->FillRule);

	// 鐗规晥 - 鍏煎 "effect"(schema) 鍜?"effects"(鏃ф牸寮?
	const FString EffectKey = JsonObj->HasField(TEXT("effect")) ? TEXT("effect") : TEXT("effects");
	if (JsonObj->HasField(EffectKey))
	{
		TArray<TSharedPtr<FJsonValue>> EffectsArr;
		const TSharedPtr<FJsonValue>& EffectField = JsonObj->TryGetField(EffectKey);
		if (EffectField->Type == EJson::Array)
			EffectsArr = EffectField->AsArray();
		else if (EffectField->Type == EJson::Object)
			EffectsArr.Add(EffectField);
		for (const auto& EffectVal : EffectsArr)
		{
			const TSharedPtr<FJsonObject>& EffectObj = EffectVal->AsObject();
			if (!EffectObj.IsValid())
				continue;

			FDesignEffect Effect;
			TryGetResolvedBoolField(EffectObj, TEXT("enabled"), Resolver, Effect.bEnabled);
			EffectObj->TryGetStringField(TEXT("blendMode"), Effect.BlendMode);

			FString TypeStr;
			EffectObj->TryGetStringField(TEXT("type"), TypeStr);
			if (TypeStr == TEXT("blur"))
				Effect.Type = EDesignEffectType::Blur;
			else if (TypeStr == TEXT("background_blur"))
				Effect.Type = EDesignEffectType::BackgroundBlur;
			else if (TypeStr == TEXT("shadow"))
				Effect.Type = EDesignEffectType::Shadow;

			TryGetResolvedNumberField(EffectObj, TEXT("radius"), Resolver, Effect.Radius);

			if (Effect.Type == EDesignEffectType::Shadow)
			{
				Effect.Shadow.BlendMode = Effect.BlendMode;

				// shadowType: "inner"|"outer" (schema 鏍煎紡)
				FString ShadowTypeStr;
				if (EffectObj->TryGetStringField(TEXT("shadowType"), ShadowTypeStr))
					Effect.Shadow.bInner = (ShadowTypeStr == TEXT("inner"));
				else
					TryGetResolvedBoolField(EffectObj, TEXT("inner"), Resolver, Effect.Shadow.bInner);

				// offset: {x, y} (schema 鏍煎紡) 鎴?offsetX/offsetY (鏃ф牸寮?
				const TSharedPtr<FJsonValue>& OffsetField = EffectObj->TryGetField(TEXT("offset"));
				if (OffsetField.IsValid() && OffsetField->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject>& OffsetObj = OffsetField->AsObject();
					TryGetResolvedNumberField(OffsetObj, TEXT("x"), Resolver, Effect.Shadow.OffsetX);
					TryGetResolvedNumberField(OffsetObj, TEXT("y"), Resolver, Effect.Shadow.OffsetY);
				}
				else
				{
					TryGetResolvedNumberField(EffectObj, TEXT("offsetX"), Resolver, Effect.Shadow.OffsetX);
					TryGetResolvedNumberField(EffectObj, TEXT("offsetY"), Resolver, Effect.Shadow.OffsetY);
				}

				TryGetResolvedNumberField(EffectObj, TEXT("blur"), Resolver, Effect.Shadow.Blur);
				TryGetResolvedNumberField(EffectObj, TEXT("spread"), Resolver, Effect.Shadow.Spread);
				FString ShadowColorStr = TEXT("#00000040");
				EffectObj->TryGetStringField(TEXT("color"), ShadowColorStr);
				if (FDesignVariableResolver::IsVariableRef(ShadowColorStr))
					Effect.Shadow.ColorSourceVar = ShadowColorStr;
				Effect.Shadow.Color = Resolver.ResolveColor(ShadowColorStr);
			}

			Node->Effects.Add(MoveTemp(Effect));
		}
	}

	// Component reference properties.
	JsonObj->TryGetStringField(TEXT("ref"), Node->Ref);
	TryGetResolvedBoolField(JsonObj, TEXT("reusable"), Resolver, Node->bReusable);

	// Parse descendants (ref instance overrides or full replacements).
	const TSharedPtr<FJsonValue>& DescField = JsonObj->TryGetField(TEXT("descendants"));
	if (DescField.IsValid() && DescField->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>& DescObj = DescField->AsObject();
		for (const auto& Pair : DescObj->Values)
		{
			if (Pair.Value->Type != EJson::Object)
				continue;

			const TSharedPtr<FJsonObject>& OverrideObj = Pair.Value->AsObject();

			// 瀵硅薄鏇挎崲妯″紡锛氬惈 type 瀛楁锛岀敤 ParseNode 瀹屾暣瑙ｆ瀽
			if (OverrideObj->HasField(TEXT("type")))
			{
				// Descendants閫氬父鏄粍浠跺疄渚嬶紝缁ф壙鐖惰妭鐐瑰潗鏍囩郴
				float DescX = 0.0f, DescY = 0.0f;
				TryGetResolvedNumberField(OverrideObj, TEXT("x"), Resolver, DescX);
				TryGetResolvedNumberField(OverrideObj, TEXT("y"), Resolver, DescY);
				if (auto FullNode = ParseNode(OverrideObj, Resolver, DescX, DescY))
					Node->Descendants.Add(Pair.Key, FullNode);
				continue;
			}

			// Property override mode.
			TSharedPtr<FDesignNode> DescNode = MakeShared<FDesignNode>();
			DescNode->FontSize = 0.0f;
			DescNode->FontWeight = TEXT("");
			DescNode->FontStyle = TEXT("");

			{
				FString RawContent;
				if (OverrideObj->TryGetStringField(TEXT("content"), RawContent))
					DescNode->Content = Resolver.ResolveString(RawContent);
			}
			TryGetResolvedNumberField(OverrideObj, TEXT("fontSize"), Resolver, DescNode->FontSize);
			OverrideObj->TryGetStringField(TEXT("fontWeight"), DescNode->FontWeight);
			OverrideObj->TryGetStringField(TEXT("fontStyle"), DescNode->FontStyle);
			TryGetResolvedBoolField(OverrideObj, TEXT("underline"), Resolver, DescNode->bUnderline);
			TryGetResolvedBoolField(OverrideObj, TEXT("strikethrough"), Resolver, DescNode->bStrikethrough);
			OverrideObj->TryGetStringField(TEXT("href"), DescNode->Href);
			{
				FString RawFontFamily;
				if (OverrideObj->TryGetStringField(TEXT("fontFamily"), RawFontFamily))
					DescNode->FontFamily = Resolver.ResolveString(RawFontFamily);
			}
			if (OverrideObj->HasField(TEXT("fill")))
			{
				const TSharedPtr<FJsonValue>& FillField = OverrideObj->TryGetField(TEXT("fill"));
				DescNode->Fill = ParseFill(FillField, Resolver);
				DescNode->Fills.Empty();  
				if (FillField.IsValid() && FillField->Type == EJson::Array)
				{
					for (const auto& Item : FillField->AsArray())
					{
						FDesignFill F = ParseFill(Item, Resolver);
						if (F.Type != EDesignFillType::None && F.bEnabled)
							DescNode->Fills.Add(MoveTemp(F));
					}
				}
				else if (DescNode->Fill.Type != EDesignFillType::None)
				{
					DescNode->Fills.Add(DescNode->Fill);
				}
			}
			{
				const TSharedPtr<FJsonValue>& StrokeOvField = OverrideObj->TryGetField(TEXT("stroke"));
				if (StrokeOvField.IsValid() && StrokeOvField->Type == EJson::Object)
				{
					DescNode->Stroke = ParseStroke(StrokeOvField->AsObject(), Resolver);
					ApplyPenStrokeExtensions(StrokeOvField->AsObject(), *DescNode, Resolver);
				}
			}

			// Size overrides (support string formats like "fill_container"/"fit_content").
			if (OverrideObj->HasField(TEXT("width")))
			{
				DescNode->Width = ParseSize(OverrideObj->TryGetField(TEXT("width")), EDesignNodeType::Frame, Resolver);
				DescNode->bHasWidth = true;
			}
			if (OverrideObj->HasField(TEXT("height")))
			{
				DescNode->Height = ParseSize(OverrideObj->TryGetField(TEXT("height")), EDesignNodeType::Frame, Resolver);
				DescNode->bHasHeight = true;
			}

			if (OverrideObj->HasField(TEXT("x")))
				DescNode->bHasX = TryGetResolvedNumberField(OverrideObj, TEXT("x"), Resolver, DescNode->X);
			if (OverrideObj->HasField(TEXT("y")))
				DescNode->bHasY = TryGetResolvedNumberField(OverrideObj, TEXT("y"), Resolver, DescNode->Y);
			TryGetResolvedBoolField(OverrideObj, TEXT("enabled"), Resolver, DescNode->bEnabled);
			if (OverrideObj->HasField(TEXT("padding")))
			{
				DescNode->Padding = ParsePadding(OverrideObj->TryGetField(TEXT("padding")), Resolver);
				DescNode->bHasPadding = true;
			}
			if (OverrideObj->HasField(TEXT("gap")))
				DescNode->bHasGap = TryGetResolvedNumberField(OverrideObj, TEXT("gap"), Resolver, DescNode->Gap);
			if (OverrideObj->HasField(TEXT("clip")))
				DescNode->bHasClip = TryGetResolvedBoolField(OverrideObj, TEXT("clip"), Resolver, DescNode->bClip);
			if (OverrideObj->HasField(TEXT("opacity")))
				DescNode->bHasOpacity = TryGetResolvedNumberField(OverrideObj, TEXT("opacity"), Resolver, DescNode->Opacity);
			{
				FString JCStr;
				if (OverrideObj->TryGetStringField(TEXT("justifyContent"), JCStr))
					DescNode->JustifyContent = ParseJustifyContent(JCStr);
			}
			{
				FString AIStr;
				if (OverrideObj->TryGetStringField(TEXT("alignItems"), AIStr))
					DescNode->AlignItems = ParseAlignItems(AIStr);
			}
			{
				FString TAStr;
				if (OverrideObj->TryGetStringField(TEXT("textAlign"), TAStr))
					DescNode->TextAlign = ParseTextAlign(TAStr);
			}
			TryGetResolvedNumberField(OverrideObj, TEXT("lineHeight"), Resolver, DescNode->LineHeight);
			{
				FString TGStr;
				if (OverrideObj->TryGetStringField(TEXT("textGrowth"), TGStr))
					DescNode->TextGrowth = ParseTextGrowth(TGStr);
			}
			OverrideObj->TryGetStringField(TEXT("name"), DescNode->Name);
			bool bPenPlaceholder = false;
			TryGetResolvedBoolField(OverrideObj, TEXT("placeholder"), Resolver, bPenPlaceholder);
			bool bPenHasSlot = false;
			int32 PenSlotNodeCount = 0;
			const TSharedPtr<FJsonValue>& DescSlotField = OverrideObj->TryGetField(TEXT("slot"));
			if (DescSlotField.IsValid() && DescSlotField->Type == EJson::Array)
			{
				bPenHasSlot = true;
				for (const TSharedPtr<FJsonValue>& SlotVal : DescSlotField->AsArray())
				{
					if (!SlotVal.IsValid())
						continue;
					FString SlotId;
					if (SlotVal->TryGetString(SlotId))
						++PenSlotNodeCount;
				}
			}
			if (bPenHasSlot)
			{
				DescNode->Metadata.Add(TEXT("pen.slot.present"), TEXT("true"));
				DescNode->Metadata.Add(TEXT("pen.slot.count"), FString::FromInt(PenSlotNodeCount));
				if (PenSlotNodeCount == 0)
					DescNode->Metadata.Add(TEXT("pen.slot.empty"), TEXT("true"));
			}

			// children 瑕嗙洊锛氬畬鏁存浛鎹㈠瓙鑺傜偣
			const TSharedPtr<FJsonValue>& ChildOvField = OverrideObj->TryGetField(TEXT("children"));
			if (ChildOvField.IsValid() && ChildOvField->Type == EJson::Array)
			{
				const auto& ChildArr = ChildOvField->AsArray();
				// 鑾峰彇DescNode鐨勭粷瀵瑰潗鏍囦綔涓哄瓙鑺傜偣鐨勭埗鍧愭爣
				float DescNodeX = 0.0f, DescNodeY = 0.0f;
				TryGetResolvedNumberField(OverrideObj, TEXT("x"), Resolver, DescNodeX);
				TryGetResolvedNumberField(OverrideObj, TEXT("y"), Resolver, DescNodeY);
				for (const auto& ChildVal : ChildArr)
				{
					if (auto Child = ParseNode(ChildVal->AsObject(), Resolver, DescNodeX, DescNodeY))
						DescNode->Children.Add(Child);
				}
			}

			ApplyPenSemanticHints(*DescNode, bPenPlaceholder, bPenHasSlot, PenSlotNodeCount);

			Node->Descendants.Add(Pair.Key, DescNode);
		}
	}
}

FDesignSize FPenParser::ParseSize(const TSharedPtr<FJsonValue>& Value, EDesignNodeType NodeType, const FDesignVariableResolver& Resolver)
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

		// 鍙橀噺寮曠敤
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

FDesignFill FPenParser::ParseFill(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver)
{
	FDesignFill Result;

	if (!Value.IsValid())
		return Result;

	// Empty array means explicit fill clear.
	if (Value->Type == EJson::Array && Value->AsArray().Num() == 0)
	{
		Result.bExplicitClear = true;
		return Result;
	}

	// 闈炵┖鏁扮粍 鈫?鍙栨渶鍚庝竴涓彲瑙佸厓绱犱綔涓轰富濉厖
	if (Value->Type == EJson::Array)
	{
		const auto& Arr = Value->AsArray();
		for (int32 i = Arr.Num() - 1; i >= 0; --i)
		{
			FDesignFill Fill = ParseFill(Arr[i], Resolver);
			if (Fill.Type != EDesignFillType::None && Fill.bEnabled)
				return Fill;
		}
		return Result;
	}

	// 瀛楃涓叉牸寮忥紙绾壊鎴栧彉閲忥級
	if (Value->Type == EJson::String)
	{
		FString Str = Value->AsString();
		if (Str == TEXT("transparent"))
			return Result;
		Result.Type = EDesignFillType::Color;
		if (FDesignVariableResolver::IsVariableRef(Str))
			Result.SourceVar = Str;
		Result.SolidColor = Resolver.ResolveColor(Str);
		return Result;
	}

	// 瀵硅薄鏍煎紡
	if (Value->Type == EJson::Object)
	{
		const auto& Obj = Value->AsObject();
		if (!Obj.IsValid())
			return Result;
		FString FillType;
		Obj->TryGetStringField(TEXT("type"), FillType);

		TryGetResolvedBoolField(Obj, TEXT("enabled"), Resolver, Result.bEnabled);
		TryGetResolvedNumberField(Obj, TEXT("opacity"), Resolver, Result.Opacity);
		Obj->TryGetStringField(TEXT("blendMode"), Result.BlendMode);

		if (FillType == TEXT("color"))
		{
			Result.Type = EDesignFillType::Color;
			FString ColorStr;
			if (Obj->TryGetStringField(TEXT("color"), ColorStr))
			{
				if (FDesignVariableResolver::IsVariableRef(ColorStr))
					Result.SourceVar = ColorStr;
				Result.SolidColor = Resolver.ResolveColor(ColorStr);
			}
		}
		else if (FillType == TEXT("gradient"))
		{
			Result.Type = EDesignFillType::Gradient;
			FString GradType;
			if (Obj->TryGetStringField(TEXT("gradientType"), GradType))
			{
				if (GradType == TEXT("radial"))
					Result.Gradient.GradientType = EDesignGradientType::Radial;
				else if (GradType == TEXT("angular"))
					Result.Gradient.GradientType = EDesignGradientType::Angular;
			}
			TryGetResolvedNumberField(Obj, TEXT("rotation"), Resolver, Result.Gradient.Rotation);

			const TSharedPtr<FJsonValue>& CenterField = Obj->TryGetField(TEXT("center"));
			if (CenterField.IsValid() && CenterField->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject>& CenterObj = CenterField->AsObject();
				const bool bHasCenterX = TryGetResolvedNumberField(CenterObj, TEXT("x"), Resolver, Result.Gradient.CenterX);
				const bool bHasCenterY = TryGetResolvedNumberField(CenterObj, TEXT("y"), Resolver, Result.Gradient.CenterY);
				Result.Gradient.bHasCenter = bHasCenterX || bHasCenterY;
			}

			const TSharedPtr<FJsonValue>& SizeField = Obj->TryGetField(TEXT("size"));
			if (SizeField.IsValid() && SizeField->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject>& SizeObj = SizeField->AsObject();
				const bool bHasSizeW = TryGetResolvedNumberField(SizeObj, TEXT("width"), Resolver, Result.Gradient.SizeWidth);
				const bool bHasSizeH = TryGetResolvedNumberField(SizeObj, TEXT("height"), Resolver, Result.Gradient.SizeHeight);
				Result.Gradient.bHasSize = bHasSizeW || bHasSizeH;
			}

			const TSharedPtr<FJsonValue>& ColorsField = Obj->TryGetField(TEXT("colors"));
			if (ColorsField.IsValid() && ColorsField->Type == EJson::Array)
			{
				for (const auto& StopVal : ColorsField->AsArray())
				{
					const auto& StopObj = StopVal->AsObject();
					if (!StopObj.IsValid())
						continue;
					FDesignGradientStop Stop;
					FString StopColorStr;
					if (StopObj->TryGetStringField(TEXT("color"), StopColorStr))
					{
						if (FDesignVariableResolver::IsVariableRef(StopColorStr))
							Stop.SourceVar = StopColorStr;
						Stop.Color = Resolver.ResolveColor(StopColorStr);
					}
					TryGetResolvedNumberField(StopObj, TEXT("position"), Resolver, Stop.Position);
					Result.Gradient.Colors.Add(Stop);
				}
			}
		}
		else if (FillType == TEXT("image"))
		{
			Result.Type = EDesignFillType::Image;
			Obj->TryGetStringField(TEXT("url"), Result.Image.Url);
			FString Mode;
			if (Obj->TryGetStringField(TEXT("mode"), Mode))
			{
				if (Mode == TEXT("fill"))
					Result.Image.Mode = EDesignImageMode::Fill;
				else if (Mode == TEXT("fit"))
					Result.Image.Mode = EDesignImageMode::Fit;
				else if (Mode == TEXT("crop"))
					Result.Image.Mode = EDesignImageMode::Crop;
			}
			if (!TryGetResolvedNumberField(Obj, TEXT("opacity"), Resolver, Result.Image.Opacity))
				Result.Image.Opacity = Result.Opacity;
			const TSharedPtr<FJsonValue>& TransformField = Obj->TryGetField(TEXT("imageTransform"));
			if (TransformField.IsValid() && TransformField->Type == EJson::Array)
			{
				const auto& TransformArr = TransformField->AsArray();
				if (TransformArr.Num() == 2)
				{
					auto Row0 = TransformArr[0]->AsArray();
					auto Row1 = TransformArr[1]->AsArray();
					if (Row0.Num() == 3 && Row1.Num() == 3)
					{
						TryGetNumberValueFlexible(Row0[0], Result.Image.Transform.A);
						TryGetNumberValueFlexible(Row0[1], Result.Image.Transform.B);
						TryGetNumberValueFlexible(Row0[2], Result.Image.Transform.TX);
						TryGetNumberValueFlexible(Row1[0], Result.Image.Transform.C);
						TryGetNumberValueFlexible(Row1[1], Result.Image.Transform.D);
						TryGetNumberValueFlexible(Row1[2], Result.Image.Transform.TY);
					}
				}
			}
		}
		else if (FillType == TEXT("mesh_gradient"))
		{
			Result.Type = EDesignFillType::Gradient;
			Result.bFromMeshGradient = true;
			Result.MeshGradientRaw = SerializeJsonValueCompact(Value);
			Result.MeshGradient.bHasColumns = TryGetResolvedNumberField(Obj, TEXT("columns"), Resolver, Result.MeshGradient.Columns);
			Result.MeshGradient.bHasRows = TryGetResolvedNumberField(Obj, TEXT("rows"), Resolver, Result.MeshGradient.Rows);

			const TSharedPtr<FJsonValue>& ColorsField = Obj->TryGetField(TEXT("colors"));
			if (ColorsField.IsValid() && ColorsField->Type == EJson::Array)
			{
				for (const auto& StopVal : ColorsField->AsArray())
				{
					const auto& StopObj = StopVal->AsObject();
					if (!StopObj.IsValid())
						continue;

					FDesignGradientStop Stop;
					FString StopColorStr;
					if (StopObj->TryGetStringField(TEXT("color"), StopColorStr))
					{
						if (FDesignVariableResolver::IsVariableRef(StopColorStr))
							Stop.SourceVar = StopColorStr;
						Stop.Color = Resolver.ResolveColor(StopColorStr);
					}

					if (!TryGetResolvedNumberField(StopObj, TEXT("position"), Resolver, Stop.Position))
					{
						Stop.Position = Result.Gradient.Colors.Num() > 0
							? 1.0f
							: 0.0f;
					}
					Result.Gradient.Colors.Add(Stop);
				}
			}

			const TSharedPtr<FJsonValue>& PointsField = Obj->TryGetField(TEXT("points"));
			if (PointsField.IsValid() && PointsField->Type == EJson::Array)
			{
				for (const TSharedPtr<FJsonValue>& PointVal : PointsField->AsArray())
				{
					if (!PointVal.IsValid() || PointVal->Type != EJson::Object)
						continue;

					const TSharedPtr<FJsonObject> PointObj = PointVal->AsObject();
					if (!PointObj.IsValid())
						continue;

					FDesignMeshGradientPoint MeshPoint;
					MeshPoint.RawJson = SerializeJsonValueCompact(PointVal);

					const bool bHasXY = TryGetResolvedNumberField(PointObj, TEXT("x"), Resolver, MeshPoint.X)
						&& TryGetResolvedNumberField(PointObj, TEXT("y"), Resolver, MeshPoint.Y);
					if (bHasXY)
					{
						MeshPoint.bHasPosition = true;
					}
					else
					{
						float PosX = 0.0f;
						float PosY = 0.0f;
						const TSharedPtr<FJsonValue>& PositionField = PointObj->TryGetField(TEXT("position"));
						if (TryParsePointXY(PositionField, Resolver, PosX, PosY))
						{
							MeshPoint.X = PosX;
							MeshPoint.Y = PosY;
							MeshPoint.bHasPosition = true;
						}
					}

					FString MeshColor;
					if (PointObj->TryGetStringField(TEXT("color"), MeshColor))
					{
						if (FDesignVariableResolver::IsVariableRef(MeshColor))
							MeshPoint.SourceVar = MeshColor;
						MeshPoint.Color = Resolver.ResolveColor(MeshColor);
					}

					TryParseMeshHandle(PointObj, Resolver, TEXT("in"), TEXT("handleIn"), MeshPoint.InHandle);
					TryParseMeshHandle(PointObj, Resolver, TEXT("out"), TEXT("handleOut"), MeshPoint.OutHandle);

					Result.MeshGradient.Points.Add(MoveTemp(MeshPoint));
				}
			}
		}
	}

	return Result;
}

FDesignStroke FPenParser::ParseStroke(const TSharedPtr<FJsonObject>& JsonObj, const FDesignVariableResolver& Resolver)
{
	FDesignStroke Result;

	if (!JsonObj.IsValid())
		return Result;

	// 瀵归綈
	FString Align;
	if (JsonObj->TryGetStringField(TEXT("align"), Align))
	{
		if (Align == TEXT("center"))
			Result.Align = EDesignStrokeAlign::Center;
		else if (Align == TEXT("outside"))
			Result.Align = EDesignStrokeAlign::Outside;
	}
	if (JsonObj->HasField(TEXT("thickness")))
	{
		const auto& ThickVal = JsonObj->TryGetField(TEXT("thickness"));
		if (TryGetNumberValueFlexible(ThickVal, Result.Thickness.Uniform))
		{
			Result.Thickness.bUniform = true;
		}
		else if (ThickVal->Type == EJson::String && FDesignVariableResolver::IsVariableRef(ThickVal->AsString()))
		{
			Result.Thickness.bUniform = true;
			Result.Thickness.Uniform = Resolver.ResolveNumber(ThickVal->AsString());
		}
		else if (ThickVal->Type == EJson::Object)
		{
			Result.Thickness.bUniform = false;
			const auto& ThickObj = ThickVal->AsObject();
			TryGetResolvedNumberField(ThickObj, TEXT("top"), Resolver, Result.Thickness.Top);
			TryGetResolvedNumberField(ThickObj, TEXT("right"), Resolver, Result.Thickness.Right);
			TryGetResolvedNumberField(ThickObj, TEXT("bottom"), Resolver, Result.Thickness.Bottom);
			TryGetResolvedNumberField(ThickObj, TEXT("left"), Resolver, Result.Thickness.Left);
		}
	}

	// 濉厖
	if (JsonObj->HasField(TEXT("fill")))
		Result.Fill = ParseFill(JsonObj->TryGetField(TEXT("fill")), Resolver);

	return Result;
}

FDesignPadding FPenParser::ParsePadding(const TSharedPtr<FJsonValue>& Value, const FDesignVariableResolver& Resolver)
{
	FDesignPadding R;
	if (!Value.IsValid()) return R;

	auto ResolvePaddingNumber = [&Resolver](const TSharedPtr<FJsonValue>& Input, float& OutNumber) -> bool
	{
		if (!Input.IsValid())
			return false;
		if (TryGetNumberValueFlexible(Input, OutNumber))
			return true;
		if (Input->Type != EJson::String)
			return false;
		const FString Raw = Input->AsString();
		if (!FDesignVariableResolver::IsVariableRef(Raw))
			return false;
		OutNumber = Resolver.ResolveNumber(Raw);
		return true;
	};

	if (ResolvePaddingNumber(Value, R.Top))
	{
		R.Right = R.Bottom = R.Left = R.Top;
	}
	else if (Value->Type == EJson::Array)
	{
		const auto& Arr = Value->AsArray();
		if (Arr.Num() == 2)
		{
			float H = 0.0f;
			float V = 0.0f;
			if (ResolvePaddingNumber(Arr[0], H))
				R.Left = R.Right = H;
			if (ResolvePaddingNumber(Arr[1], V))
				R.Top = R.Bottom = V;
		}
		else if (Arr.Num() >= 4)
		{
			ResolvePaddingNumber(Arr[0], R.Top);
			ResolvePaddingNumber(Arr[1], R.Right);
			ResolvePaddingNumber(Arr[2], R.Bottom);
			ResolvePaddingNumber(Arr[3], R.Left);
		}
	}
	return R;
}

EDesignNodeType FPenParser::ParseNodeType(const FString& S)
{
	if (S == TEXT("frame")) return EDesignNodeType::Frame;
	if (S == TEXT("rectangle")) return EDesignNodeType::Rectangle;
	if (S == TEXT("ellipse")) return EDesignNodeType::Ellipse;
	if (S == TEXT("line")) return EDesignNodeType::Line;
	if (S == TEXT("polygon")) return EDesignNodeType::Polygon;
	if (S == TEXT("text")) return EDesignNodeType::Text;
	if (S == TEXT("icon_font")) return EDesignNodeType::IconFont;
	if (S == TEXT("path")) return EDesignNodeType::Path;
	if (S == TEXT("ref")) return EDesignNodeType::Ref;
	if (S == TEXT("group")) return EDesignNodeType::Group;
	if (S == TEXT("connection")) return EDesignNodeType::Connection;
	if (S == TEXT("note")) return EDesignNodeType::Note;
	if (S == TEXT("prompt")) return EDesignNodeType::Prompt;
	if (S == TEXT("context")) return EDesignNodeType::Context;
	return EDesignNodeType::Unknown;
}

EDesignLayout FPenParser::ParseLayout(const FString& S)
{
	if (S == TEXT("vertical")) return EDesignLayout::Vertical;
	if (S == TEXT("horizontal")) return EDesignLayout::Horizontal;
	if (S == TEXT("wrap")) return EDesignLayout::Wrap;
	if (S == TEXT("grid")) return EDesignLayout::Grid;
	return EDesignLayout::None;
}

EDesignJustifyContent FPenParser::ParseJustifyContent(const FString& S)
{
	if (S == TEXT("center")) return EDesignJustifyContent::Center;
	if (S == TEXT("end")) return EDesignJustifyContent::End;
	if (S == TEXT("space_between")) return EDesignJustifyContent::SpaceBetween;
	if (S == TEXT("space_around")) return EDesignJustifyContent::SpaceAround;
	return EDesignJustifyContent::Start;
}

EDesignAlignItems FPenParser::ParseAlignItems(const FString& S)
{
	if (S == TEXT("center")) return EDesignAlignItems::Center;
	if (S == TEXT("end")) return EDesignAlignItems::End;
	return EDesignAlignItems::Start;
}

EDesignTextAlign FPenParser::ParseTextAlign(const FString& S)
{
	if (S == TEXT("center")) return EDesignTextAlign::Center;
	if (S == TEXT("right")) return EDesignTextAlign::Right;
	if (S == TEXT("justify")) return EDesignTextAlign::Justify;
	return EDesignTextAlign::Left;
}

EDesignTextAlignVertical FPenParser::ParseTextAlignVertical(const FString& S)
{
	if (S == TEXT("middle")) return EDesignTextAlignVertical::Middle;
	if (S == TEXT("bottom")) return EDesignTextAlignVertical::Bottom;
	return EDesignTextAlignVertical::Top;
}

EDesignTextGrowth FPenParser::ParseTextGrowth(const FString& S)
{
	if (S == TEXT("fixed-width")) return EDesignTextGrowth::FixedWidth;
	if (S == TEXT("fixed-width-height")) return EDesignTextGrowth::FixedWidthHeight;
	return EDesignTextGrowth::Auto;
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
		Info.Width = Child->Width.IsFixed() ? Child->Width.Value : 0.0f;
		Info.Height = Child->Height.IsFixed() ? Child->Height.Value : 0.0f;
		Result.Add(MoveTemp(Info));
	}
	return Result;
}





