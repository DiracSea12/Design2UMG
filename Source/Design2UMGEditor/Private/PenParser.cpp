// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "PenParser.h"
#include "Design2UMGLog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
}

bool FPenParser::ParseFromFile(const FString& FilePath, FPenDocument& OutDocument)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenParser: Failed to load file: %s"), *FilePath);
		return false;
	}
	return ParseDocument(JsonString, OutDocument);
}

bool FPenParser::ParseDocument(const FString& JsonString, FPenDocument& OutDocument)
{
	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenParser: Failed to parse JSON"));
		return false;
	}
	return ParseDocumentInternal(JsonObj, OutDocument);
}

bool FPenParser::ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FPenDocument& OutDocument)
{
	if (!JsonObj.IsValid())
	{
		UE_LOG(LogDesign2UMG, Error, TEXT("PenParser: Invalid root json object"));
		return false;
	}

	if (!JsonObj->TryGetStringField(TEXT("version"), OutDocument.Version) || OutDocument.Version.IsEmpty())
	{
		OutDocument.Version = TEXT("1.0");
		UE_LOG(LogDesign2UMG, Warning, TEXT("PenParser: missing 'version', fallback to %s"), *OutDocument.Version);
	}

	// 解析字体
	if (JsonObj->HasField(TEXT("fonts")))
	{
		const TArray<TSharedPtr<FJsonValue>>& FontsArr = JsonObj->GetArrayField(TEXT("fonts"));
		for (const auto& FontVal : FontsArr)
		{
			const TSharedPtr<FJsonObject>& FontObj = FontVal->AsObject();
			FPenFont Font;
			Font.Name = FontObj->GetStringField(TEXT("name"));
			Font.Url = FontObj->GetStringField(TEXT("url"));
			OutDocument.Fonts.Add(Font);
		}
	}

	// 解析主题
	if (JsonObj->HasField(TEXT("themes")))
	{
		const TSharedPtr<FJsonObject>& ThemesObj = JsonObj->GetObjectField(TEXT("themes"));
		for (const auto& Pair : ThemesObj->Values)
		{
			TArray<FString> Values;
			const TArray<TSharedPtr<FJsonValue>>& Arr = Pair.Value->AsArray();
			for (const auto& Val : Arr)
				Values.Add(Val->AsString());
			OutDocument.Themes.Add(Pair.Key, Values);
		}
	}

	// 解析变量
	if (JsonObj->HasField(TEXT("variables")))
	{
		ParseVariables(JsonObj->GetObjectField(TEXT("variables")), OutDocument.Variables);
	}

	// 创建变量解析器
	FPenVariableResolver Resolver(OutDocument.Variables);

	// 解析子节点
	if (JsonObj->HasField(TEXT("children")))
	{
		const TArray<TSharedPtr<FJsonValue>>& ChildrenArr = JsonObj->GetArrayField(TEXT("children"));
		for (const auto& ChildVal : ChildrenArr)
		{
			if (auto Node = ParseNode(ChildVal->AsObject(), Resolver))
				OutDocument.Children.Add(Node);
		}
	}

	return true;
}

void FPenParser::ParseVariables(const TSharedPtr<FJsonObject>& JsonObj, TMap<FString, FPenVariable>& OutVariables)
{
	for (const auto& Pair : JsonObj->Values)
	{
		FPenVariable Var;
		Var.Name = Pair.Key;

		const TSharedPtr<FJsonObject>& VarObj = Pair.Value->AsObject();
		FString TypeStr = VarObj->GetStringField(TEXT("type"));

		if (TypeStr == TEXT("boolean"))
			Var.Type = EPenVariableType::Boolean;
		else if (TypeStr == TEXT("color"))
			Var.Type = EPenVariableType::Color;
		else if (TypeStr == TEXT("number"))
			Var.Type = EPenVariableType::Number;
		else
			Var.Type = EPenVariableType::String;

		// 解析值（可能是单值或主题值数组）
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

				// 第一个条目作为默认值
				if (i == 0)
					Var.Value = EntryValue;

				// 保存主题值条目
				FPenThemeValue TV;
				TV.Value = EntryValue;
				if (EntryObj->HasField(TEXT("theme")))
				{
					const TSharedPtr<FJsonObject>& ThemeObj = EntryObj->GetObjectField(TEXT("theme"));
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

TSharedPtr<FPenNode> FPenParser::ParseNode(const TSharedPtr<FJsonObject>& JsonObj, const FPenVariableResolver& Resolver)
{
	if (!JsonObj.IsValid())
		return nullptr;

	TSharedPtr<FPenNode> Node = MakeShared<FPenNode>();

	// 基础属性
	Node->Id = JsonObj->GetStringField(TEXT("id"));
	JsonObj->TryGetStringField(TEXT("name"), Node->Name);
	Node->Type = ParseNodeType(JsonObj->GetStringField(TEXT("type")));

	// 节点级主题设置 - 影响当前节点及子节点的变量解析
	const FPenVariableResolver* ActiveResolver = &Resolver;
	FPenVariableResolver ThemedResolver;
	if (JsonObj->HasField(TEXT("theme")))
	{
		const TSharedPtr<FJsonObject>& ThemeObj = JsonObj->GetObjectField(TEXT("theme"));
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

	// 位置
	if (JsonObj->HasField(TEXT("x")))
		Node->bHasX = TryGetNumberFieldFlexible(JsonObj, TEXT("x"), Node->X);
	if (JsonObj->HasField(TEXT("y")))
		Node->bHasY = TryGetNumberFieldFlexible(JsonObj, TEXT("y"), Node->Y);
	TryGetNumberFieldFlexible(JsonObj, TEXT("rotation"), Node->Rotation);
	if (JsonObj->HasField(TEXT("size")))
	{
		const auto& SizeObj = JsonObj->GetObjectField(TEXT("size"));
		float SizeWidth = 0.0f;
		if (TryGetNumberFieldFlexible(SizeObj, TEXT("width"), SizeWidth))
		{
			Node->Width = FPenSize::Fixed(SizeWidth);
			Node->bHasWidth = true;
		}
		float SizeHeight = 0.0f;
		if (TryGetNumberFieldFlexible(SizeObj, TEXT("height"), SizeHeight))
		{
			Node->Height = FPenSize::Fixed(SizeHeight);
			Node->bHasHeight = true;
		}
	}

	// 顶层 width/height 优先级更高
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

	// 可见性
	if (JsonObj->HasField(TEXT("enabled")))
		Node->bEnabled = JsonObj->GetBoolField(TEXT("enabled"));
	if (JsonObj->HasField(TEXT("opacity")))
		Node->bHasOpacity = TryGetNumberFieldFlexible(JsonObj, TEXT("opacity"), Node->Opacity);
	JsonObj->TryGetBoolField(TEXT("flipX"), Node->bFlipX);
	JsonObj->TryGetBoolField(TEXT("flipY"), Node->bFlipY);

	// 布局属性 - Frame 默认 Horizontal，Group 默认 None
	if (JsonObj->HasField(TEXT("layout")))
		Node->Layout = ParseLayout(JsonObj->GetStringField(TEXT("layout")));
	else if (Node->Type == EPenNodeType::Group)
		Node->Layout = EPenLayout::None;
	if (JsonObj->HasField(TEXT("gap")))
		Node->bHasGap = TryGetNumberFieldFlexible(JsonObj, TEXT("gap"), Node->Gap);
	if (JsonObj->HasField(TEXT("padding")))
	{
		Node->Padding = ParsePadding(JsonObj->TryGetField(TEXT("padding")));
		Node->bHasPadding = true;
	}
	if (JsonObj->HasField(TEXT("justifyContent")))
		Node->JustifyContent = ParseJustifyContent(JsonObj->GetStringField(TEXT("justifyContent")));
	if (JsonObj->HasField(TEXT("alignItems")))
		Node->AlignItems = ParseAlignItems(JsonObj->GetStringField(TEXT("alignItems")));
	if (JsonObj->HasField(TEXT("clip")))
		Node->bHasClip = JsonObj->TryGetBoolField(TEXT("clip"), Node->bClip);
	JsonObj->TryGetBoolField(TEXT("placeholder"), Node->bPlaceholder);

	// 视觉属性
	if (JsonObj->HasField(TEXT("fill")))
	{
		const TSharedPtr<FJsonValue>& FillField = JsonObj->TryGetField(TEXT("fill"));
		Node->Fill = ParseFill(FillField, *ActiveResolver);
		// 多层填充：fill 为非空数组时，逐个解析到 Fills
		if (FillField.IsValid() && FillField->Type == EJson::Array)
		{
			const auto& Arr = FillField->AsArray();
			for (const auto& Item : Arr)
			{
				FPenFill F = ParseFill(Item, *ActiveResolver);
				if (F.Type != EPenFillType::None && F.bEnabled)
					Node->Fills.Add(MoveTemp(F));
			}
		}
		else if (Node->Fill.Type != EPenFillType::None)
		{
			Node->Fills.Add(Node->Fill);
		}
	}
	if (JsonObj->HasField(TEXT("stroke")))
		Node->Stroke = ParseStroke(JsonObj->GetObjectField(TEXT("stroke")), *ActiveResolver);
	if (JsonObj->HasField(TEXT("cornerRadius")))
		Node->CornerRadius = ActiveResolver->ResolveCornerRadius(JsonObj->TryGetField(TEXT("cornerRadius")));

	// 继续解析其他属性...
	ParseNodeExtended(JsonObj, Node, *ActiveResolver);

	// 子节点（继承主题上下文）
	if (JsonObj->HasField(TEXT("children")))
	{
		const auto& ChildrenArr = JsonObj->GetArrayField(TEXT("children"));
		for (const auto& ChildVal : ChildrenArr)
		{
			if (auto Child = ParseNode(ChildVal->AsObject(), *ActiveResolver))
				Node->Children.Add(Child);
		}
	}

	return Node;
}

void FPenParser::ParseNodeExtended(const TSharedPtr<FJsonObject>& JsonObj, TSharedPtr<FPenNode>& Node, const FPenVariableResolver& Resolver)
{
	// 文本属性
	JsonObj->TryGetStringField(TEXT("content"), Node->Content);
	JsonObj->TryGetStringField(TEXT("fontFamily"), Node->FontFamily);
	TryGetNumberFieldFlexible(JsonObj, TEXT("fontSize"), Node->FontSize);
	JsonObj->TryGetStringField(TEXT("fontWeight"), Node->FontWeight);
	TryGetNumberFieldFlexible(JsonObj, TEXT("lineHeight"), Node->LineHeight);
	TryGetNumberFieldFlexible(JsonObj, TEXT("letterSpacing"), Node->LetterSpacing);

	if (JsonObj->HasField(TEXT("textAlign")))
		Node->TextAlign = ParseTextAlign(JsonObj->GetStringField(TEXT("textAlign")));
	if (JsonObj->HasField(TEXT("textAlignVertical")))
		Node->TextAlignVertical = ParseTextAlignVertical(JsonObj->GetStringField(TEXT("textAlignVertical")));
	if (JsonObj->HasField(TEXT("textGrowth")))
		Node->TextGrowth = ParseTextGrowth(JsonObj->GetStringField(TEXT("textGrowth")));

	// 图标字体属性
	JsonObj->TryGetStringField(TEXT("iconFontName"), Node->IconFontName);
	JsonObj->TryGetStringField(TEXT("iconFontFamily"), Node->IconFontFamily);
	TryGetNumberFieldFlexible(JsonObj, TEXT("weight"), Node->IconWeight);
	TryGetNumberFieldFlexible(JsonObj, TEXT("innerRadius"), Node->InnerRadius);
	TryGetNumberFieldFlexible(JsonObj, TEXT("startAngle"), Node->StartAngle);
	TryGetNumberFieldFlexible(JsonObj, TEXT("sweepAngle"), Node->SweepAngle);
	TryGetNumberFieldFlexible(JsonObj, TEXT("polygonCount"), Node->PolygonCount);
	JsonObj->TryGetStringField(TEXT("geometry"), Node->Geometry);
	JsonObj->TryGetStringField(TEXT("fillRule"), Node->FillRule);

	// 特效 - 兼容 "effect"(schema) 和 "effects"(旧格式)
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

			FPenEffect Effect;
			if (EffectObj->HasField(TEXT("enabled")))
				Effect.bEnabled = EffectObj->GetBoolField(TEXT("enabled"));

			FString TypeStr;
			EffectObj->TryGetStringField(TEXT("type"), TypeStr);
			if (TypeStr == TEXT("blur"))
				Effect.Type = EPenEffectType::Blur;
			else if (TypeStr == TEXT("background_blur"))
				Effect.Type = EPenEffectType::BackgroundBlur;
			else if (TypeStr == TEXT("shadow"))
				Effect.Type = EPenEffectType::Shadow;

			TryGetNumberFieldFlexible(EffectObj, TEXT("radius"), Effect.Radius);

			if (Effect.Type == EPenEffectType::Shadow)
			{
				// shadowType: "inner"|"outer" (schema 格式)
				FString ShadowTypeStr;
				if (EffectObj->TryGetStringField(TEXT("shadowType"), ShadowTypeStr))
					Effect.Shadow.bInner = (ShadowTypeStr == TEXT("inner"));
				else
					EffectObj->TryGetBoolField(TEXT("inner"), Effect.Shadow.bInner);

				// offset: {x, y} (schema 格式) 或 offsetX/offsetY (旧格式)
				if (EffectObj->HasField(TEXT("offset")))
				{
					const TSharedPtr<FJsonObject>& OffsetObj = EffectObj->GetObjectField(TEXT("offset"));
					TryGetNumberFieldFlexible(OffsetObj, TEXT("x"), Effect.Shadow.OffsetX);
					TryGetNumberFieldFlexible(OffsetObj, TEXT("y"), Effect.Shadow.OffsetY);
				}
				else
				{
					TryGetNumberFieldFlexible(EffectObj, TEXT("offsetX"), Effect.Shadow.OffsetX);
					TryGetNumberFieldFlexible(EffectObj, TEXT("offsetY"), Effect.Shadow.OffsetY);
				}

				TryGetNumberFieldFlexible(EffectObj, TEXT("blur"), Effect.Shadow.Blur);
				TryGetNumberFieldFlexible(EffectObj, TEXT("spread"), Effect.Shadow.Spread);
				FString ShadowColorStr = EffectObj->HasField(TEXT("color"))
					? EffectObj->GetStringField(TEXT("color"))
					: TEXT("#00000040");
				if (FPenVariableResolver::IsVariableRef(ShadowColorStr))
					Effect.Shadow.ColorSourceVar = ShadowColorStr;
				Effect.Shadow.Color = Resolver.ResolveColor(ShadowColorStr);
			}

			Node->Effects.Add(MoveTemp(Effect));
		}
	}

	// 组件引用属性
	JsonObj->TryGetStringField(TEXT("ref"), Node->Ref);
	JsonObj->TryGetBoolField(TEXT("reusable"), Node->bReusable);

	// 解析 descendants（ref 实例的属性覆盖或对象替换）
	if (JsonObj->HasField(TEXT("descendants")))
	{
		const TSharedPtr<FJsonObject>& DescObj = JsonObj->GetObjectField(TEXT("descendants"));
		for (const auto& Pair : DescObj->Values)
		{
			if (Pair.Value->Type != EJson::Object)
				continue;

			const TSharedPtr<FJsonObject>& OverrideObj = Pair.Value->AsObject();

			// 对象替换模式：含 type 字段，用 ParseNode 完整解析
			if (OverrideObj->HasField(TEXT("type")))
			{
				if (auto FullNode = ParseNode(OverrideObj, Resolver))
					Node->Descendants.Add(Pair.Key, FullNode);
				continue;
			}

			// 属性覆盖模式
			TSharedPtr<FPenNode> DescNode = MakeShared<FPenNode>();
			DescNode->FontSize = 0.0f;
			DescNode->FontWeight = TEXT("");

			OverrideObj->TryGetStringField(TEXT("content"), DescNode->Content);
			TryGetNumberFieldFlexible(OverrideObj, TEXT("fontSize"), DescNode->FontSize);
			OverrideObj->TryGetStringField(TEXT("fontWeight"), DescNode->FontWeight);
			OverrideObj->TryGetStringField(TEXT("fontFamily"), DescNode->FontFamily);
			if (OverrideObj->HasField(TEXT("fill")))
			{
				const TSharedPtr<FJsonValue>& FillField = OverrideObj->TryGetField(TEXT("fill"));
				DescNode->Fill = ParseFill(FillField, Resolver);
				DescNode->Fills.Empty();  
				if (FillField.IsValid() && FillField->Type == EJson::Array)
				{
					for (const auto& Item : FillField->AsArray())
					{
						FPenFill F = ParseFill(Item, Resolver);
						if (F.Type != EPenFillType::None && F.bEnabled)
							DescNode->Fills.Add(MoveTemp(F));
					}
				}
				else if (DescNode->Fill.Type != EPenFillType::None)
				{
					DescNode->Fills.Add(DescNode->Fill);
				}
			}
			if (OverrideObj->HasField(TEXT("stroke")))
				DescNode->Stroke = ParseStroke(OverrideObj->GetObjectField(TEXT("stroke")), Resolver);

			// 尺寸覆盖（支持字符串格式如 "fill_container"、"fit_content"）
			if (OverrideObj->HasField(TEXT("width")))
			{
				DescNode->Width = ParseSize(OverrideObj->TryGetField(TEXT("width")), EPenNodeType::Frame, Resolver);
				DescNode->bHasWidth = true;
			}
			if (OverrideObj->HasField(TEXT("height")))
			{
				DescNode->Height = ParseSize(OverrideObj->TryGetField(TEXT("height")), EPenNodeType::Frame, Resolver);
				DescNode->bHasHeight = true;
			}

			if (OverrideObj->HasField(TEXT("x")))
				DescNode->bHasX = TryGetNumberFieldFlexible(OverrideObj, TEXT("x"), DescNode->X);
			if (OverrideObj->HasField(TEXT("y")))
				DescNode->bHasY = TryGetNumberFieldFlexible(OverrideObj, TEXT("y"), DescNode->Y);
			if (OverrideObj->HasField(TEXT("enabled")))
				DescNode->bEnabled = OverrideObj->GetBoolField(TEXT("enabled"));
			if (OverrideObj->HasField(TEXT("padding")))
			{
				DescNode->Padding = ParsePadding(OverrideObj->TryGetField(TEXT("padding")));
				DescNode->bHasPadding = true;
			}
			if (OverrideObj->HasField(TEXT("gap")))
				DescNode->bHasGap = TryGetNumberFieldFlexible(OverrideObj, TEXT("gap"), DescNode->Gap);
			if (OverrideObj->HasField(TEXT("clip")))
				DescNode->bHasClip = OverrideObj->TryGetBoolField(TEXT("clip"), DescNode->bClip);
			if (OverrideObj->HasField(TEXT("opacity")))
				DescNode->bHasOpacity = TryGetNumberFieldFlexible(OverrideObj, TEXT("opacity"), DescNode->Opacity);
			if (OverrideObj->HasField(TEXT("justifyContent")))
				DescNode->JustifyContent = ParseJustifyContent(OverrideObj->GetStringField(TEXT("justifyContent")));
			if (OverrideObj->HasField(TEXT("alignItems")))
				DescNode->AlignItems = ParseAlignItems(OverrideObj->GetStringField(TEXT("alignItems")));
			if (OverrideObj->HasField(TEXT("textAlign")))
				DescNode->TextAlign = ParseTextAlign(OverrideObj->GetStringField(TEXT("textAlign")));
			TryGetNumberFieldFlexible(OverrideObj, TEXT("lineHeight"), DescNode->LineHeight);
			if (OverrideObj->HasField(TEXT("textGrowth")))
				DescNode->TextGrowth = ParseTextGrowth(OverrideObj->GetStringField(TEXT("textGrowth")));

			// children 覆盖：完整替换子节点
			if (OverrideObj->HasField(TEXT("children")))
			{
				const auto& ChildArr = OverrideObj->GetArrayField(TEXT("children"));
				for (const auto& ChildVal : ChildArr)
				{
					if (auto Child = ParseNode(ChildVal->AsObject(), Resolver))
						DescNode->Children.Add(Child);
				}
			}

			Node->Descendants.Add(Pair.Key, DescNode);
		}
	}
}

FPenSize FPenParser::ParseSize(const TSharedPtr<FJsonValue>& Value, EPenNodeType NodeType, const FPenVariableResolver& Resolver)
{
	if (!Value.IsValid())
		return FPenSize::Fit();

	// 数值格式
	if (Value->Type == EJson::Number)
	{
		float NumVal = Value->AsNumber();
		// line 节点特例：0 是合法的固定值
		if (NumVal == 0 && NodeType != EPenNodeType::Line)
			return FPenSize::Fit();
		return FPenSize::Fixed(NumVal);
	}

	// 字符串格式
	if (Value->Type == EJson::String)
	{
		FString Str = Value->AsString();

		// 变量引用
		if (FPenVariableResolver::IsVariableRef(Str))
		{
			float Resolved = Resolver.ResolveNumber(Str);
			// 与数值路径保持一致：0 → Fit()（Line节点除外）
			if (Resolved == 0 && NodeType != EPenNodeType::Line)
				return FPenSize::Fit();
			return FPenSize::Fixed(Resolved);
		}

		// fill_container / fill_container(N)
		if (Str.StartsWith(TEXT("fill_container")))
		{
			float Fallback = 0.0f;
			int32 Start = Str.Find(TEXT("("));
			if (Start != INDEX_NONE)
			{
				int32 End = Str.Find(TEXT(")"));
				FString NumStr = Str.Mid(Start + 1, End - Start - 1);
				Fallback = FCString::Atof(*NumStr);
			}
			return FPenSize::Fill(Fallback);
		}

		// fit_content / fit_content(N)
		if (Str.StartsWith(TEXT("fit_content")))
		{
			float Min = 0.0f;
			int32 Start = Str.Find(TEXT("("));
			if (Start != INDEX_NONE)
			{
				int32 End = Str.Find(TEXT(")"));
				FString NumStr = Str.Mid(Start + 1, End - Start - 1);
				Min = FCString::Atof(*NumStr);
			}
			return FPenSize::Fit(Min);
		}

		// 尝试解析为数值
		float NumVal = FCString::Atof(*Str);
		if (NumVal > 0)
			return FPenSize::Fixed(NumVal);
	}

	return FPenSize::Fit();
}

FPenFill FPenParser::ParseFill(const TSharedPtr<FJsonValue>& Value, const FPenVariableResolver& Resolver)
{
	FPenFill Result;

	if (!Value.IsValid())
		return Result;

	// 空数组（显式清除填充）
	if (Value->Type == EJson::Array && Value->AsArray().Num() == 0)
	{
		Result.bExplicitClear = true;
		return Result;
	}

	// 非空数组 → 取最后一个可见元素作为主填充
	if (Value->Type == EJson::Array)
	{
		const auto& Arr = Value->AsArray();
		for (int32 i = Arr.Num() - 1; i >= 0; --i)
		{
			FPenFill Fill = ParseFill(Arr[i], Resolver);
			if (Fill.Type != EPenFillType::None && Fill.bEnabled)
				return Fill;
		}
		return Result;
	}

	// 字符串格式（纯色或变量）
	if (Value->Type == EJson::String)
	{
		FString Str = Value->AsString();
		if (Str == TEXT("transparent"))
			return Result;
		Result.Type = EPenFillType::Color;
		if (FPenVariableResolver::IsVariableRef(Str))
			Result.SourceVar = Str;
		Result.SolidColor = Resolver.ResolveColor(Str);
		return Result;
	}

	// 对象格式
	if (Value->Type == EJson::Object)
	{
		const auto& Obj = Value->AsObject();
		FString FillType = Obj->GetStringField(TEXT("type"));

		if (Obj->HasField(TEXT("enabled")))
			Result.bEnabled = Obj->GetBoolField(TEXT("enabled"));

		if (FillType == TEXT("color"))
		{
			Result.Type = EPenFillType::Color;
			FString ColorStr = Obj->GetStringField(TEXT("color"));
			if (FPenVariableResolver::IsVariableRef(ColorStr))
				Result.SourceVar = ColorStr;
			Result.SolidColor = Resolver.ResolveColor(ColorStr);
		}
		else if (FillType == TEXT("gradient"))
		{
			Result.Type = EPenFillType::Gradient;
			if (Obj->HasField(TEXT("gradientType")))
			{
				FString GradType = Obj->GetStringField(TEXT("gradientType"));
				if (GradType == TEXT("radial"))
					Result.Gradient.GradientType = EPenGradientType::Radial;
				else if (GradType == TEXT("angular"))
					Result.Gradient.GradientType = EPenGradientType::Angular;
			}
			TryGetNumberFieldFlexible(Obj, TEXT("rotation"), Result.Gradient.Rotation);

			if (Obj->HasField(TEXT("colors")))
			{
				for (const auto& StopVal : Obj->GetArrayField(TEXT("colors")))
				{
					const auto& StopObj = StopVal->AsObject();
					FPenGradientStop Stop;
					FString StopColorStr = StopObj->GetStringField(TEXT("color"));
					if (FPenVariableResolver::IsVariableRef(StopColorStr))
						Stop.SourceVar = StopColorStr;
					Stop.Color = Resolver.ResolveColor(StopColorStr);
					TryGetNumberFieldFlexible(StopObj, TEXT("position"), Stop.Position);
					Result.Gradient.Colors.Add(Stop);
				}
			}
		}
		else if (FillType == TEXT("image"))
		{
			Result.Type = EPenFillType::Image;
			Result.Image.Url = Obj->GetStringField(TEXT("url"));
			if (Obj->HasField(TEXT("mode")))
			{
				FString Mode = Obj->GetStringField(TEXT("mode"));
				if (Mode == TEXT("fill"))
					Result.Image.Mode = EPenImageMode::Fill;
				else if (Mode == TEXT("fit"))
					Result.Image.Mode = EPenImageMode::Fit;
				else if (Mode == TEXT("crop"))
					Result.Image.Mode = EPenImageMode::Crop;
			}
			TryGetNumberFieldFlexible(Obj, TEXT("opacity"), Result.Image.Opacity);
			if (Obj->HasField(TEXT("imageTransform")))
			{
				const auto& TransformArr = Obj->GetArrayField(TEXT("imageTransform"));
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
	}

	return Result;
}

FPenStroke FPenParser::ParseStroke(const TSharedPtr<FJsonObject>& JsonObj, const FPenVariableResolver& Resolver)
{
	FPenStroke Result;

	if (!JsonObj.IsValid())
		return Result;

	// 对齐
	if (JsonObj->HasField(TEXT("align")))
	{
		FString Align = JsonObj->GetStringField(TEXT("align"));
		if (Align == TEXT("center"))
			Result.Align = EPenStrokeAlign::Center;
		else if (Align == TEXT("outside"))
			Result.Align = EPenStrokeAlign::Outside;
	}
	if (JsonObj->HasField(TEXT("thickness")))
	{
		const auto& ThickVal = JsonObj->TryGetField(TEXT("thickness"));
		if (TryGetNumberValueFlexible(ThickVal, Result.Thickness.Uniform))
		{
			Result.Thickness.bUniform = true;
		}
		else if (ThickVal->Type == EJson::Object)
		{
			Result.Thickness.bUniform = false;
			const auto& ThickObj = ThickVal->AsObject();
			TryGetNumberFieldFlexible(ThickObj, TEXT("top"), Result.Thickness.Top);
			TryGetNumberFieldFlexible(ThickObj, TEXT("right"), Result.Thickness.Right);
			TryGetNumberFieldFlexible(ThickObj, TEXT("bottom"), Result.Thickness.Bottom);
			TryGetNumberFieldFlexible(ThickObj, TEXT("left"), Result.Thickness.Left);
		}
	}

	// 填充
	if (JsonObj->HasField(TEXT("fill")))
		Result.Fill = ParseFill(JsonObj->TryGetField(TEXT("fill")), Resolver);

	return Result;
}

FPenPadding FPenParser::ParsePadding(const TSharedPtr<FJsonValue>& Value)
{
	FPenPadding R;
	if (!Value.IsValid()) return R;

	if (TryGetNumberValueFlexible(Value, R.Top))
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
			if (TryGetNumberValueFlexible(Arr[0], H))
				R.Left = R.Right = H;
			if (TryGetNumberValueFlexible(Arr[1], V))
				R.Top = R.Bottom = V;
		}
		else if (Arr.Num() >= 4)
		{
			TryGetNumberValueFlexible(Arr[0], R.Top);
			TryGetNumberValueFlexible(Arr[1], R.Right);
			TryGetNumberValueFlexible(Arr[2], R.Bottom);
			TryGetNumberValueFlexible(Arr[3], R.Left);
		}
	}
	return R;
}

EPenNodeType FPenParser::ParseNodeType(const FString& S)
{
	if (S == TEXT("frame")) return EPenNodeType::Frame;
	if (S == TEXT("rectangle")) return EPenNodeType::Rectangle;
	if (S == TEXT("ellipse")) return EPenNodeType::Ellipse;
	if (S == TEXT("line")) return EPenNodeType::Line;
	if (S == TEXT("polygon")) return EPenNodeType::Polygon;
	if (S == TEXT("text")) return EPenNodeType::Text;
	if (S == TEXT("icon_font")) return EPenNodeType::IconFont;
	if (S == TEXT("path")) return EPenNodeType::Path;
	if (S == TEXT("ref")) return EPenNodeType::Ref;
	if (S == TEXT("group")) return EPenNodeType::Group;
	if (S == TEXT("connection")) return EPenNodeType::Connection;
	if (S == TEXT("note")) return EPenNodeType::Note;
	if (S == TEXT("prompt")) return EPenNodeType::Prompt;
	if (S == TEXT("context")) return EPenNodeType::Context;
	return EPenNodeType::Unknown;
}

EPenLayout FPenParser::ParseLayout(const FString& S)
{
	if (S == TEXT("vertical")) return EPenLayout::Vertical;
	if (S == TEXT("horizontal")) return EPenLayout::Horizontal;
	return EPenLayout::None;
}

EPenJustifyContent FPenParser::ParseJustifyContent(const FString& S)
{
	if (S == TEXT("center")) return EPenJustifyContent::Center;
	if (S == TEXT("end")) return EPenJustifyContent::End;
	if (S == TEXT("space_between")) return EPenJustifyContent::SpaceBetween;
	if (S == TEXT("space_around")) return EPenJustifyContent::SpaceAround;
	return EPenJustifyContent::Start;
}

EPenAlignItems FPenParser::ParseAlignItems(const FString& S)
{
	if (S == TEXT("center")) return EPenAlignItems::Center;
	if (S == TEXT("end")) return EPenAlignItems::End;
	return EPenAlignItems::Start;
}

EPenTextAlign FPenParser::ParseTextAlign(const FString& S)
{
	if (S == TEXT("center")) return EPenTextAlign::Center;
	if (S == TEXT("right")) return EPenTextAlign::Right;
	if (S == TEXT("justify")) return EPenTextAlign::Justify;
	return EPenTextAlign::Left;
}

EPenTextAlignVertical FPenParser::ParseTextAlignVertical(const FString& S)
{
	if (S == TEXT("middle")) return EPenTextAlignVertical::Middle;
	if (S == TEXT("bottom")) return EPenTextAlignVertical::Bottom;
	return EPenTextAlignVertical::Top;
}

EPenTextGrowth FPenParser::ParseTextGrowth(const FString& S)
{
	if (S == TEXT("fixed-width")) return EPenTextGrowth::FixedWidth;
	if (S == TEXT("fixed-width-height")) return EPenTextGrowth::FixedWidthHeight;
	return EPenTextGrowth::Auto;
}

TArray<FPenFrameInfo> ExtractTopFrames(const FPenDocument& Document)
{
	TArray<FPenFrameInfo> Result;
	for (const auto& Child : Document.Children)
	{
		if (!Child.IsValid() || Child->Type != EPenNodeType::Frame)
			continue;

		FPenFrameInfo Info;
		Info.Id = Child->Id;
		Info.Name = Child->Name.IsEmpty() ? Child->Id : Child->Name;
		Info.Width = Child->Width.IsFixed() ? Child->Width.Value : 0.0f;
		Info.Height = Child->Height.IsFixed() ? Child->Height.Value : 0.0f;
		Result.Add(MoveTemp(Info));
	}
	return Result;
}
