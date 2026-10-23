// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT
#include "PencilScript.h"
#include "PencilScriptProcess.h"
#include "RefExpander.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignVariableResolver.h"
#include "DesignTreeLimits.h"
#include "PenParserInternal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Misc/DefaultValueHelper.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersionComparison.h"

namespace PencilScript
{
namespace
{
	bool Fail(FString& Error, const FString& Message) { Error = Message; return false; }
	FString Hash(const FString& Text) { FTCHARToUTF8 Bytes(*Text); return FMD5::HashBytes(reinterpret_cast<const uint8*>(Bytes.Get()), Bytes.Length()); }
	FString Trim(FString Text) { Text.TrimStartAndEndInline(); return Text; }
	bool IsName(const FString& Text)
	{
		if (Text.IsEmpty() || !(FChar::IsAlpha(Text[0]) || Text[0] == '_')) return false;
		for (TCHAR Ch : Text) if (!(FChar::IsAlnum(Ch) || Ch == '_')) return false;
		return true;
	}
	bool Unquote(const FString& Text, FString& Out)
	{
		if (Text.Len() < 2 || Text[0] != '"' || Text[Text.Len() - 1] != '"') return false;
		Out.Reset();
		for (int32 I = 1; I < Text.Len() - 1; ++I)
		{
			TCHAR Ch = Text[I];
			if (Ch == '\\')
			{
				if (++I >= Text.Len() - 1) return false;
				Ch = Text[I];
			}
			else if (Ch == '"') return false;
			Out.AppendChar(Ch);
		}
		return true;
	}
	bool SplitArguments(const FString& Text, TArray<FString>& Out)
	{
		TCHAR Quote = 0; bool Escaped = false; int32 Start = 0;
		for (int32 I = 0; I < Text.Len(); ++I)
		{
			const TCHAR Ch = Text[I];
			if (Quote)
			{
				if (Escaped) Escaped = false;
				else if (Ch == '\\') Escaped = true;
				else if (Ch == Quote) Quote = 0;
			}
			else if (Ch == '"' || Ch == '\'') Quote = Ch;
			else if (Ch == ',') { Out.Add(Trim(Text.Mid(Start, I - Start))); Start = I + 1; }
		}
		if (Quote) return false;
		if (Start < Text.Len()) Out.Add(Trim(Text.Mid(Start)));
		return true;
	}
	bool Number(const FString& Text, double& Out)
	{
		return FDefaultValueHelper::ParseDouble(Text, Out) && FMath::IsFinite(Out);
	}
	FString JSON(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text));
		return Text;
	}
}

bool BuildInputs(const FString& Source, const TSharedPtr<FJsonObject>& Overrides,
	const FDesignVariableResolver& Resolver, TSharedPtr<FJsonObject>& Out, FString& Error, TArray<FString>* OutReferences)
{
	Out.Reset(); Error.Reset();
	if (OutReferences) OutReferences->Reset();
	auto Inputs = MakeShared<FJsonObject>();
	bool HasSchema = false;
	int32 Cursor = 0;
	while ((Cursor = Source.Find(TEXT("/**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor)) != INDEX_NONE)
	{
		const int32 End = Source.Find(TEXT("*/"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Cursor + 3);
		if (End == INDEX_NONE) return Fail(Error, TEXT("Unclosed script documentation comment."));
		FString Comment = Source.Mid(Cursor + 3, End - Cursor - 3);
		Cursor = End + 2;
		TArray<FString> Lines; Comment.ParseIntoArrayLines(Lines, false);
		for (FString Line : Lines)
		{
			Line = Trim(Line); if (Line.StartsWith(TEXT("*"))) Line = Trim(Line.Mid(1));
			if (Line.StartsWith(TEXT("@schema ")))
			{
				if (HasSchema || Trim(Line.Mid(8)) != TEXT("2.11")) return Fail(Error, TEXT("Script requires one @schema 2.11 declaration."));
				HasSchema = true; continue;
			}
			if (!Line.StartsWith(TEXT("@input "))) continue;
			Line = Trim(Line.Mid(7));
			int32 Colon = INDEX_NONE;
			if (!Line.FindChar(':', Colon)) return Fail(Error, TEXT("Script input requires name: type."));
			const FString Name = Trim(Line.Left(Colon));
			if (!IsName(Name) || Inputs->HasField(Name)) return Fail(Error, TEXT("Invalid or duplicate script input name."));
			FString Declaration = Trim(Line.Mid(Colon + 1));
			int32 TypeEnd = 0;
			while (TypeEnd < Declaration.Len() && FChar::IsAlpha(Declaration[TypeEnd])) ++TypeEnd;
			const FString Type = Declaration.Left(TypeEnd);
			FString Tail = Trim(Declaration.Mid(TypeEnd));
			TArray<FString> Arguments;
			if (Tail.StartsWith(TEXT("(")))
			{
				int32 Close = 1; TCHAR Quote = 0; bool Escaped = false;
				for (; Close < Tail.Len(); ++Close)
				{
					const TCHAR Ch = Tail[Close];
					if (Quote) { if (Escaped) Escaped = false; else if (Ch == '\\') Escaped = true; else if (Ch == Quote) Quote = 0; }
					else if (Ch == '"' || Ch == '\'') Quote = Ch;
					else if (Ch == ')') break;
				}
				if (Close == Tail.Len() || !SplitArguments(Tail.Mid(1, Close - 1), Arguments)) return Fail(Error, TEXT("Invalid script input arguments."));
				Tail = Trim(Tail.Mid(Close + 1));
			}
			const bool HasDefault = Tail.StartsWith(TEXT("="));
			if (!Tail.IsEmpty() && !HasDefault) return Fail(Error, TEXT("Invalid script input default declaration."));
			const FString Default = HasDefault ? Trim(Tail.Mid(1)) : FString();
			TSharedPtr<FJsonValue> Value;
			const TSharedPtr<FJsonValue> Override = Overrides.IsValid() ? Overrides->TryGetField(Name) : nullptr;
			FString OverrideText;
			if (Override.IsValid() && Override->Type == EJson::String)
			{
				const FString Text = Override->AsString();
				if (Text.StartsWith(TEXT("\\$"))) OverrideText = Text.RightChop(1);
				else
				{
					OverrideText = Resolver.ResolveString(Text);
					if (FDesignVariableResolver::IsVariableRef(OverrideText)) return Fail(Error, TEXT("Unresolved script input variable: ") + Name);
				}
			}
			if (Type == TEXT("number"))
			{
				double Min = -DBL_MAX, Max = DBL_MAX, NumberValue = 0;
				for (const FString& Arg : Arguments)
				{
					FString Key, Text;
					if (!Arg.Split(TEXT("="), &Key, &Text)) return Fail(Error, TEXT("Number input arguments require min=/max=."));
					double Bound;
					if (!Number(Trim(Text), Bound)) return Fail(Error, TEXT("Invalid number input bound."));
					if (Trim(Key) == TEXT("min")) Min = Bound; else if (Trim(Key) == TEXT("max")) Max = Bound;
					else return Fail(Error, TEXT("Unknown number input bound."));
				}
				if (Min > Max || (HasDefault && !Number(Default, NumberValue))) return Fail(Error, TEXT("Invalid number input default/range."));
				if (Override.IsValid())
				{
					if (Override->Type == EJson::Number) NumberValue = Override->AsNumber();
					else if (Override->Type != EJson::String || !Number(OverrideText, NumberValue)) return Fail(Error, TEXT("Invalid numeric script input: ") + Name);
				}
				if (!FMath::IsFinite(NumberValue)) return Fail(Error, TEXT("Non-finite script input."));
				Value = MakeShared<FJsonValueNumber>(FMath::Clamp(NumberValue, Min, Max));
			}
			else if (Type == TEXT("boolean"))
			{
				if (Arguments.Num() || (HasDefault && Default != TEXT("true") && Default != TEXT("false"))) return Fail(Error, TEXT("Invalid boolean input declaration."));
				bool Boolean = Default == TEXT("true");
				if (Override.IsValid())
				{
					if (Override->Type == EJson::Boolean) Boolean = Override->AsBool();
					else if (Override->Type == EJson::String && (OverrideText == TEXT("true") || OverrideText == TEXT("false"))) Boolean = OverrideText == TEXT("true");
					else return Fail(Error, TEXT("Invalid boolean script input: ") + Name);
				}
				Value = MakeShared<FJsonValueBoolean>(Boolean);
			}
			else if (Type == TEXT("string") || Type == TEXT("color") || Type == TEXT("ref") || Type == TEXT("enum"))
			{
				TArray<FString> Options;
				for (const FString& Arg : Arguments)
				{
					FString Option;
					if (Type != TEXT("enum") || !Unquote(Arg, Option)) return Fail(Error, TEXT("Enum input requires quoted options."));
					Options.Add(Option);
				}
				if (Type == TEXT("enum") && Options.IsEmpty()) return Fail(Error, TEXT("Empty enum script input."));
				FString Text = Type == TEXT("color") ? TEXT("#000000") : (Options.Num() ? Options[0] : FString());
				if (HasDefault && !Unquote(Default, Text))
				{
					if (Type != TEXT("color") && Type != TEXT("ref")) return Fail(Error, TEXT("String input default must be quoted."));
					Text = Default;
				}
				if (Override.IsValid())
				{
					if (Override->Type != EJson::String) return Fail(Error, TEXT("Invalid string script input: ") + Name);
					Text = OverrideText;
				}
				if (Type == TEXT("enum") && !Options.Contains(Text)) return Fail(Error, TEXT("Unknown enum script input value: ") + Name);
				if (Type == TEXT("ref") && OutReferences && !Text.IsEmpty()) OutReferences->AddUnique(Text);
				Value = MakeShared<FJsonValueString>(Text);
			}
			else return Fail(Error, TEXT("Unknown script input type: ") + Type);
			Inputs->SetField(Name, Value);
		}
	}
	if (!HasSchema) return Fail(Error, TEXT("Script is missing @schema 2.11."));
	Out = Inputs; return true;
}

TArray<FString> CollectInputReferences(const TSharedPtr<FDesignNode>& Node,
	const FDesignDocument& Document, const TMap<FString, FString>& Theme)
{
	TArray<FString> References;
	const auto Properties = Node->Source().Pencil.Properties;
	FString Path, Source, Error;
	if (!Properties.IsValid() || !Properties->TryGetStringField(TEXT("scriptUri"), Path)
		|| IFileManager::Get().FileSize(*Path) > 1024 * 1024 || !FFileHelper::LoadFileToString(Source, *Path)) return References;
	const auto Inputs = Properties->TryGetField(TEXT("inputs"));
	if (Inputs.IsValid() && Inputs->Type != EJson::Object) return References;
	FDesignVariableResolver Resolver(Document.Variables);
	Resolver.SetThemeContext(FDesignVariableResolver::BuildDocumentTheme(Document.Themes));
	Resolver.PushThemeOverride(Theme);
	TSharedPtr<FJsonObject> Values;
	if (!BuildInputs(Source, Inputs.IsValid() ? Inputs->AsObject() : nullptr, Resolver, Values, Error, &References)) References.Reset();
	return References;
}

bool ParseNodes(const FString& Text, const FString& Scope, const FString& SourcePath,
	const FDesignVariableResolver& Resolver, TArray<TSharedPtr<FDesignNode>>& Out, FString& Error)
{
	Out.Reset(); Error.Reset();
	TArray<TSharedPtr<FJsonValue>> Roots;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Roots)) return Fail(Error, TEXT("Script result must be a JSON array."));
	struct FPending { TSharedPtr<FJsonValue> Value; FString Path; int32 Depth; bool bNode; };
	TArray<FPending> Pending;
	for (int32 I = 0; I < Roots.Num(); ++I) Pending.Add({Roots[I], FString::FromInt(I), 0, true});
	TMap<FString, FString> Ids;
	const FString Prefix = TEXT("sc_") + Hash(Scope) + TEXT("_");
	int32 Count = 0;
	TArray<TSharedPtr<FJsonObject>> Objects;
	while (!Pending.IsEmpty())
	{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		FPending Item = Pending.Pop(false);
#else
		FPending Item = Pending.Pop(EAllowShrinking::No);
#endif
		if (++Count > DesignTreeLimits::MaxParsedNodeCount || Item.Depth > DesignTreeLimits::MaxNodeDepth) return Fail(Error, TEXT("Generated script tree exceeds the design tree budget."));
		if (!Item.Value.IsValid() || Item.Value->Type != EJson::Object) return Fail(Error, TEXT("Script node must be an object."));
		auto Object = Item.Value->AsObject();
		Objects.Add(Object);
		if (Item.bNode)
		{
			FString Type, Id;
			if (!Object->TryGetStringField(TEXT("type"), Type) || PenParser::ParseNodeType(Type) == EDesignNodeType::Unknown) return Fail(Error, TEXT("Unknown or missing generated node type: ") + Type);
			if (Object->HasField(TEXT("id")))
			{
				if (!Object->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty() || Id.Contains(TEXT("/")) || Ids.Contains(Id)) return Fail(Error, TEXT("Invalid or duplicate generated node ID."));
				const FString Scoped = Prefix + TEXT("e_") + Hash(Id);
				Ids.Add(Id, Scoped); Object->SetStringField(TEXT("id"), Scoped);
			}
			else Object->SetStringField(TEXT("id"), Prefix + TEXT("a_") + Hash(Item.Path));
		}
		const auto Children = Object->TryGetField(TEXT("children"));
		if (Children.IsValid())
		{
			if (Children->Type != EJson::Array) return Fail(Error, TEXT("Generated children must be an array."));
			for (int32 I = 0; I < Children->AsArray().Num(); ++I) Pending.Add({Children->AsArray()[I], Item.Path + TEXT(".") + FString::FromInt(I), Item.Depth + 1, true});
		}
		const auto Descendants = Object->TryGetField(TEXT("descendants"));
		if (Descendants.IsValid() && Descendants->Type == EJson::Object)
		{
			for (const auto& Pair : Descendants->AsObject()->Values)
			{
				if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object) return Fail(Error, TEXT("Generated descendant must be an object."));
				Pending.Add({Pair.Value, Item.Path + TEXT(".descendants.") + Pair.Key, Item.Depth + 1,
					Pair.Value->AsObject()->HasField(TEXT("type"))});
			}
		}
	}
	// Rewrite only node/reference semantics; metadata and input strings remain source values.
	for (int32 I = 0; I < Objects.Num(); ++I)
	{
		auto Object = Objects[I]; FString Ref;
		if (Object->TryGetStringField(TEXT("ref"), Ref)) if (const FString* Scoped = Ids.Find(Ref)) Object->SetStringField(TEXT("ref"), *Scoped);
		const auto Descendants = Object->TryGetField(TEXT("descendants"));
		if (Descendants.IsValid() && Descendants->Type == EJson::Object)
		{
			auto Renamed = MakeShared<FJsonObject>();
			for (const auto& Pair : Descendants->AsObject()->Values)
			{
				TArray<FString> Segments; FString(*Pair.Key).ParseIntoArray(Segments, TEXT("/"), false);
				for (FString& Segment : Segments) if (const FString* Scoped = Ids.Find(Segment)) Segment = *Scoped;
				Renamed->SetField(FString::Join(Segments, TEXT("/")), Pair.Value);
			}
			Object->SetObjectField(TEXT("descendants"), Renamed);
		}
	}
	return PenParser::ParseGeneratedNodes(Roots, SourcePath, Resolver, Out, Error);
}

bool Expand(const TSharedPtr<FDesignNode>& Node, const FDesignVariableResolver& Resolver,
	const FString& Scope, const FString& DocumentPath, TSharedPtr<FDesignNode>& Out, FString& Error)
{
	Out.Reset();
	FString Path, Source;
	const auto Properties = Node->Source().Pencil.Properties;
	if (!Properties.IsValid() || !Properties->TryGetStringField(TEXT("scriptUri"), Path) || Path.IsEmpty()) return Fail(Error, TEXT("Script node is missing scriptUri."));
	if (IFileManager::Get().FileSize(*Path) > 1024 * 1024) return Fail(Error, TEXT("Script source exceeds 1 MiB."));
	if (!FFileHelper::LoadFileToString(Source, *Path)) return Fail(Error, TEXT("Script source read failed: ") + Path);
	TSharedPtr<FJsonObject> Inputs;
	const auto InputValue = Properties->TryGetField(TEXT("inputs"));
	if (InputValue.IsValid() && InputValue->Type != EJson::Object) return Fail(Error, TEXT("Script inputs must be an object."));
	if (!BuildInputs(Source, InputValue.IsValid() ? InputValue->AsObject() : nullptr, Resolver, Inputs, Error)) return false;
	const double Width = Node->Transform.Width.Value, Height = Node->Transform.Height.Value;
	if (!FMath::IsFinite(Width) || !FMath::IsFinite(Height) || Width < 0 || Height < 0) return Fail(Error, TEXT("Script requires finite resolved dimensions."));
	auto Input = MakeShared<FJsonObject>();
	Input->SetNumberField(TEXT("width"), Width); Input->SetNumberField(TEXT("height"), Height); Input->SetObjectField(TEXT("input"), Inputs);
	// Pencil 0.6.77 uses Mulberry32, seed zero, reset for each script invocation.
	const FString Program = TEXT("(()=>{let state=0;Math.random=()=>{state=(state+0x6D2B79F5)|0;let t=Math.imul(state^(state>>>15),1|state);t=(t+Math.imul(t^(t>>>7),61|t))^t;return ((t^(t>>>14))>>>0)/4294967296;};const value=((pencil)=>{\n")
		+ Source + TEXT("\n})(") + JSON(Input) + TEXT(");if(!Array.isArray(value))throw new TypeError('Script must return a node array');const text=JSON.stringify(value);if(!Array.isArray(JSON.parse(text)))throw new TypeError('Script serialization must be an array');return text;})()");
	FString Result;
	if (!PencilScriptProcess::Evaluate(Program, Result, Error)) return false;
	TArray<TSharedPtr<FDesignNode>> Children;
	if (!ParseNodes(Result, Scope, DocumentPath, Resolver, Children, Error)) return false;
	Out = RefExpander::DeepCopyNode(Node); Out->Children = MoveTemp(Children);
	return true;
}
}
