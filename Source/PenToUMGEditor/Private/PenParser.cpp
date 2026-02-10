// Copyright Epic Games, Inc. All Rights Reserved.

#include "PenParser.h"
#include "PenToUMGLog.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"

bool FPenParser::ParseFromFile(const FString& FilePath, FPenDocument& OutDocument)
{
	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogPenToUMG, Error, TEXT("PenParser: Failed to load file: %s"), *FilePath);
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
		UE_LOG(LogPenToUMG, Error, TEXT("PenParser: Failed to parse JSON"));
		return false;
	}
	return ParseDocumentInternal(JsonObj, OutDocument);
}

bool FPenParser::ParseDocumentInternal(const TSharedPtr<FJsonObject>& JsonObj, FPenDocument& OutDocument)
{
	OutDocument.Version = JsonObj->GetStringField(TEXT("version"));

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
			// 主题值数组，取第一个作为默认值
			const TArray<TSharedPtr<FJsonValue>>& Arr = ValueField->AsArray();
			if (Arr.Num() > 0)
			{
				const TSharedPtr<FJsonObject>& FirstObj = Arr[0]->AsObject();
				const TSharedPtr<FJsonValue>& Val = FirstObj->TryGetField(TEXT("value"));
				if (Val.IsValid())
				{
					if (Val->Type == EJson::String)
						Var.Value = Val->AsString();
					else if (Val->Type == EJson::Number)
						Var.Value = FString::SanitizeFloat(Val->AsNumber());
					else if (Val->Type == EJson::Boolean)
						Var.Value = Val->AsBool() ? TEXT("true") : TEXT("false");
				}
			}
		}
		else
		{
			// 单值
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

	// 位置
	if (JsonObj->HasField(TEXT("x")))
		Node->bHasX = JsonObj->TryGetNumberField(TEXT("x"), Node->X);
	if (JsonObj->HasField(TEXT("y")))
		Node->bHasY = JsonObj->TryGetNumberField(TEXT("y"), Node->Y);
	JsonObj->TryGetNumberField(TEXT("rotation"), Node->Rotation);

	// 尺寸 - 先检查 size 对象（兼容非标准格式）
	if (JsonObj->HasField(TEXT("size")))
	{
		const auto& SizeObj = JsonObj->GetObjectField(TEXT("size"));
		if (SizeObj->HasField(TEXT("width")))
			Node->Width = FPenSize::Fixed(SizeObj->GetNumberField(TEXT("width")));
		if (SizeObj->HasField(TEXT("height")))
			Node->Height = FPenSize::Fixed(SizeObj->GetNumberField(TEXT("height")));
	}

	// 顶层 width/height 优先级更高
	if (JsonObj->HasField(TEXT("width")))
		Node->Width = ParseSize(JsonObj->TryGetField(TEXT("width")), Node->Type, Resolver);
	if (JsonObj->HasField(TEXT("height")))
		Node->Height = ParseSize(JsonObj->TryGetField(TEXT("height")), Node->Type, Resolver);

	// 可见性
	if (JsonObj->HasField(TEXT("enabled")))
		Node->bEnabled = JsonObj->GetBoolField(TEXT("enabled"));
	JsonObj->TryGetNumberField(TEXT("opacity"), Node->Opacity);
	JsonObj->TryGetBoolField(TEXT("flipX"), Node->bFlipX);
	JsonObj->TryGetBoolField(TEXT("flipY"), Node->bFlipY);

	// 布局属性 - 根据节点类型设置默认值（Frame默认horizontal，Group默认none）
	if (JsonObj->HasField(TEXT("layout")))
		Node->Layout = ParseLayout(JsonObj->GetStringField(TEXT("layout")));
	else if (Node->Type == EPenNodeType::Group)
		Node->Layout = EPenLayout::None;  // Group 默认 none
	// Frame 保持默认 Horizontal（在 FPenNode 中定义）
	JsonObj->TryGetNumberField(TEXT("gap"), Node->Gap);
	if (JsonObj->HasField(TEXT("padding")))
		Node->Padding = ParsePadding(JsonObj->TryGetField(TEXT("padding")));
	if (JsonObj->HasField(TEXT("justifyContent")))
		Node->JustifyContent = ParseJustifyContent(JsonObj->GetStringField(TEXT("justifyContent")));
	if (JsonObj->HasField(TEXT("alignItems")))
		Node->AlignItems = ParseAlignItems(JsonObj->GetStringField(TEXT("alignItems")));
	JsonObj->TryGetBoolField(TEXT("clip"), Node->bClip);
	JsonObj->TryGetBoolField(TEXT("placeholder"), Node->bPlaceholder);

	// 视觉属性
	if (JsonObj->HasField(TEXT("fill")))
		Node->Fill = ParseFill(JsonObj->TryGetField(TEXT("fill")), Resolver);
	if (JsonObj->HasField(TEXT("stroke")))
		Node->Stroke = ParseStroke(JsonObj->GetObjectField(TEXT("stroke")), Resolver);
	if (JsonObj->HasField(TEXT("cornerRadius")))
		Node->CornerRadius = Resolver.ResolveCornerRadius(JsonObj->TryGetField(TEXT("cornerRadius")));

	// 继续解析其他属性...
	ParseNodeExtended(JsonObj, Node, Resolver);

	// 子节点
	if (JsonObj->HasField(TEXT("children")))
	{
		const auto& ChildrenArr = JsonObj->GetArrayField(TEXT("children"));
		for (const auto& ChildVal : ChildrenArr)
		{
			if (auto Child = ParseNode(ChildVal->AsObject(), Resolver))
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
	JsonObj->TryGetNumberField(TEXT("fontSize"), Node->FontSize);
	JsonObj->TryGetStringField(TEXT("fontWeight"), Node->FontWeight);
	JsonObj->TryGetNumberField(TEXT("lineHeight"), Node->LineHeight);
	JsonObj->TryGetNumberField(TEXT("letterSpacing"), Node->LetterSpacing);

	if (JsonObj->HasField(TEXT("textAlign")))
		Node->TextAlign = ParseTextAlign(JsonObj->GetStringField(TEXT("textAlign")));
	if (JsonObj->HasField(TEXT("textAlignVertical")))
		Node->TextAlignVertical = ParseTextAlignVertical(JsonObj->GetStringField(TEXT("textAlignVertical")));
	if (JsonObj->HasField(TEXT("textGrowth")))
		Node->TextGrowth = ParseTextGrowth(JsonObj->GetStringField(TEXT("textGrowth")));

	// 图标字体属性
	JsonObj->TryGetStringField(TEXT("iconFontName"), Node->IconFontName);
	JsonObj->TryGetStringField(TEXT("iconFontFamily"), Node->IconFontFamily);
	JsonObj->TryGetNumberField(TEXT("weight"), Node->IconWeight);

	// 椭圆属性
	JsonObj->TryGetNumberField(TEXT("innerRadius"), Node->InnerRadius);
	JsonObj->TryGetNumberField(TEXT("startAngle"), Node->StartAngle);
	JsonObj->TryGetNumberField(TEXT("sweepAngle"), Node->SweepAngle);

	// 多边形属性
	JsonObj->TryGetNumberField(TEXT("polygonCount"), Node->PolygonCount);

	// 路径属性
	JsonObj->TryGetStringField(TEXT("geometry"), Node->Geometry);
	JsonObj->TryGetStringField(TEXT("fillRule"), Node->FillRule);

	// 组件引用属性
	JsonObj->TryGetStringField(TEXT("ref"), Node->Ref);
	JsonObj->TryGetBoolField(TEXT("reusable"), Node->bReusable);

	// 解析 descendants（ref 实例的属性覆盖）
	if (JsonObj->HasField(TEXT("descendants")))
	{
		const TSharedPtr<FJsonObject>& DescObj = JsonObj->GetObjectField(TEXT("descendants"));
		for (const auto& Pair : DescObj->Values)
		{
			if (Pair.Value->Type == EJson::Object)
			{
				TSharedPtr<FPenNode> DescNode = MakeShared<FPenNode>();
				// 重置默认值，避免污染覆盖判断
				DescNode->FontSize = 0.0f;
				DescNode->FontWeight = TEXT("");
				// 只解析覆盖的属性，不递归解析完整节点
				const TSharedPtr<FJsonObject>& OverrideObj = Pair.Value->AsObject();
				OverrideObj->TryGetStringField(TEXT("content"), DescNode->Content);
				OverrideObj->TryGetNumberField(TEXT("fontSize"), DescNode->FontSize);
				OverrideObj->TryGetStringField(TEXT("fontWeight"), DescNode->FontWeight);
				if (OverrideObj->HasField(TEXT("fill")))
					DescNode->Fill = ParseFill(OverrideObj->TryGetField(TEXT("fill")), Resolver);
				if (OverrideObj->TryGetNumberField(TEXT("width"), DescNode->Width.Value))
					DescNode->Width.Mode = EPenSizeMode::Fixed;
				if (OverrideObj->TryGetNumberField(TEXT("height"), DescNode->Height.Value))
					DescNode->Height.Mode = EPenSizeMode::Fixed;
				if (OverrideObj->HasField(TEXT("x")))
					DescNode->bHasX = OverrideObj->TryGetNumberField(TEXT("x"), DescNode->X);
				if (OverrideObj->HasField(TEXT("y")))
					DescNode->bHasY = OverrideObj->TryGetNumberField(TEXT("y"), DescNode->Y);
				if (OverrideObj->HasField(TEXT("enabled")))
					DescNode->bEnabled = OverrideObj->GetBoolField(TEXT("enabled"));
				Node->Descendants.Add(Pair.Key, DescNode);
			}
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

	// 空数组（清除填充）
	if (Value->Type == EJson::Array && Value->AsArray().Num() == 0)
		return Result;

	// 字符串格式（纯色或变量）
	if (Value->Type == EJson::String)
	{
		FString Str = Value->AsString();
		if (Str == TEXT("transparent"))
			return Result;
		Result.Type = EPenFillType::Color;
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
			Result.SolidColor = Resolver.ResolveColor(Obj->GetStringField(TEXT("color")));
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
			Obj->TryGetNumberField(TEXT("rotation"), Result.Gradient.Rotation);

			if (Obj->HasField(TEXT("colors")))
			{
				for (const auto& StopVal : Obj->GetArrayField(TEXT("colors")))
				{
					const auto& StopObj = StopVal->AsObject();
					FPenGradientStop Stop;
					Stop.Color = Resolver.ResolveColor(StopObj->GetStringField(TEXT("color")));
					Stop.Position = StopObj->GetNumberField(TEXT("position"));
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
			}
			Obj->TryGetNumberField(TEXT("opacity"), Result.Image.Opacity);
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

	// 厚度
	if (JsonObj->HasField(TEXT("thickness")))
	{
		const auto& ThickVal = JsonObj->TryGetField(TEXT("thickness"));
		if (ThickVal->Type == EJson::Number)
		{
			Result.Thickness.bUniform = true;
			Result.Thickness.Uniform = ThickVal->AsNumber();
		}
		else if (ThickVal->Type == EJson::Object)
		{
			Result.Thickness.bUniform = false;
			const auto& ThickObj = ThickVal->AsObject();
			ThickObj->TryGetNumberField(TEXT("top"), Result.Thickness.Top);
			ThickObj->TryGetNumberField(TEXT("right"), Result.Thickness.Right);
			ThickObj->TryGetNumberField(TEXT("bottom"), Result.Thickness.Bottom);
			ThickObj->TryGetNumberField(TEXT("left"), Result.Thickness.Left);
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

	if (Value->Type == EJson::Number)
	{
		R.Top = R.Right = R.Bottom = R.Left = Value->AsNumber();
	}
	else if (Value->Type == EJson::Array)
	{
		const auto& Arr = Value->AsArray();
		if (Arr.Num() == 2)
		{
			R.Left = R.Right = Arr[0]->AsNumber();
			R.Top = R.Bottom = Arr[1]->AsNumber();
		}
		else if (Arr.Num() >= 4)
		{
			R.Top = Arr[0]->AsNumber();
			R.Right = Arr[1]->AsNumber();
			R.Bottom = Arr[2]->AsNumber();
			R.Left = Arr[3]->AsNumber();
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
