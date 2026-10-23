// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Misc/BufferedOutputDevice.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Design2UMGConverter.h"
#include "DesignReferenceExpansion.h"
#include "DesignSchemaSourceExtensions.h"
#include "PenParser.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UMGSchemaSourceExtensions.h"
#include "UObject/UnrealType.h"

namespace PencilDocumentCorpus
{
using FJsonValues = TArray<TSharedPtr<FJsonValue>>;

FString Text(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key)
{
	FString Value;
	Object->TryGetStringField(Key, Value);
	return Value;
}

FString GitBlobHash(const TArray<uint8>& Bytes)
{
	const FTCHARToUTF8 Header(*FString::Printf(TEXT("blob %d"), Bytes.Num()));
	FSHA1 Hash;
	Hash.Update(reinterpret_cast<const uint8*>(Header.Get()), Header.Length() + 1);
	Hash.Update(Bytes.GetData(), Bytes.Num());
	Hash.Final();
	uint8 Digest[FSHA1::DigestSize];
	Hash.GetHash(Digest);
	return BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

bool ReadObject(const FString& Json, TSharedPtr<FJsonObject>& OutObject)
{
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
}

bool WriteObject(const FString& Path, const TSharedRef<FJsonObject>& Object)
{
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	return FJsonSerializer::Serialize(Object, Writer)
		&& FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

TSharedRef<FJsonObject> Counts(const TMap<FString, int32>& Values)
{
	const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	for (const auto& Pair : Values) Object->SetNumberField(Pair.Key, Pair.Value);
	return Object;
}

class FScopedMessages final : public FBufferedOutputDevice
{
public:
	FScopedMessages(FJsonValues& InMessages, const TCHAR* InStage, FOutputDeviceRedirector* InRedirector = GLog)
		: Messages(InMessages), Stage(InStage), Redirector(InRedirector)
	{
		SetVerbosity(ELogVerbosity::Warning);
		if (Redirector)
		{
			Redirector->AddOutputDevice(this);
			// Registration can replay history; the scope starts after that replay.
			TArray<FBufferedLine> Backlog;
			GetContents(Backlog);
		}
	}
	~FScopedMessages()
	{
		if (Redirector) Redirector->RemoveOutputDevice(this);
		TArray<FBufferedLine> Lines;
		GetContents(Lines);
		for (const FBufferedLine& Line : Lines)
		{
			const TSharedRef<FJsonObject> Event = MakeShared<FJsonObject>();
			Event->SetStringField(TEXT("stage"), Stage);
			Event->SetStringField(TEXT("severity"), Line.Verbosity <= ELogVerbosity::Error ? TEXT("error") : TEXT("warning"));
			Event->SetStringField(TEXT("category"), Line.Category.ToString());
			Event->SetStringField(TEXT("message"), Line.Data.Get());
			Messages.Add(MakeShared<FJsonValueObject>(Event));
		}
	}
	virtual bool CanBeUsedOnAnyThread() const override { return true; }
	virtual bool CanBeUsedOnMultipleThreads() const override { return true; }
private:
	FJsonValues& Messages;
	FString Stage;
	FOutputDeviceRedirector* Redirector;
};

struct FSourceFacts
{
	TMap<FString, FString> TypesById;
	TMap<FString, int32> Types;
	int32 DynamicObjects = 0;
};

FSourceFacts InspectSource(const TSharedPtr<FJsonObject>& Object)
{
	FSourceFacts Facts;
	FJsonValues Pending;
	Pending.Add(MakeShared<FJsonValueObject>(Object));
	while (!Pending.IsEmpty())
	{
		const TSharedPtr<FJsonValue> Value = Pending.Pop();
		if (Value->Type == EJson::Array) Pending.Append(Value->AsArray());
		if (Value->Type != EJson::Object) continue;
		const TSharedPtr<FJsonObject> Item = Value->AsObject();
		const FString Type = Text(Item, TEXT("type"));
		const FString Id = Text(Item, TEXT("id"));
		if (!Id.IsEmpty() && !Type.IsEmpty())
		{
			Facts.TypesById.Add(Id, Type);
			++Facts.Types.FindOrAdd(Type);
		}
		if (Type == TEXT("script") || Type == TEXT("browser") || Type == TEXT("shader")) ++Facts.DynamicObjects;
		for (const auto& Field : Item->Values) Pending.Add(Field.Value);
	}
	return Facts;
}

struct FDesignFacts
{
	int32 Nodes = 0;
	int32 UnknownNodes = 0;
	TMap<FString, int32> Types;
	TMap<FString, int32> SourceMappings;
	FJsonValues FrameMappings;
	TArray<TSharedPtr<FDesignNode>> References;
};

FDesignFacts InspectDesign(const TArray<TSharedPtr<FDesignNode>>& Roots, const FSourceFacts& Source)
{
	FDesignFacts Facts;
	TArray<TSharedPtr<FDesignNode>> Pending = Roots;
	while (!Pending.IsEmpty())
	{
		const TSharedPtr<FDesignNode> Node = Pending.Pop();
		if (!Node.IsValid()) continue;
		++Facts.Nodes;
		const FString Type = StaticEnum<EDesignNodeType>()->GetNameStringByValue(static_cast<int64>(Node->Type));
		++Facts.Types.FindOrAdd(Type);
		FString OriginalType = Node->Source().SourceNodeType;
		if (OriginalType.IsEmpty()) OriginalType = Source.TypesById.FindRef(Node->Id);
		++Facts.SourceMappings.FindOrAdd(OriginalType + TEXT(" -> ") + Type);
		if (Node->Type == EDesignNodeType::Unknown) ++Facts.UnknownNodes;
		if (Node->Type == EDesignNodeType::Ref) Facts.References.Add(Node);
		if (Node->Type == EDesignNodeType::Frame && !OriginalType.IsEmpty()
			&& OriginalType != TEXT("frame") && OriginalType != TEXT("ref"))
		{
			const TSharedRef<FJsonObject> Mapping = MakeShared<FJsonObject>();
			Mapping->SetStringField(TEXT("id"), Node->Id);
			Mapping->SetStringField(TEXT("source_type"), OriginalType);
			Facts.FrameMappings.Add(MakeShared<FJsonValueObject>(Mapping));
		}
		Pending.Append(Node->Children);
	}
	return Facts;
}

int32 InspectWidgets(const FUMGWidgetNode& Root, TMap<FString, int32>& Types, int32& WithSourceIdentity)
{
	int32 Count = 0;
	TArray<const FUMGWidgetNode*> Pending;
	Pending.Add(&Root);
	while (!Pending.IsEmpty())
	{
		const FUMGWidgetNode* Node = Pending.Pop();
		if (Node->Type.IsEmpty()) continue;
		++Count;
		++Types.FindOrAdd(Node->Type);
		if (!Node->Source().SourceNodeId.IsEmpty()) ++WithSourceIdentity;
		for (const FUMGWidgetNode& Child : Node->Children) Pending.Add(&Child);
	}
	return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilCorpusDiagnosticsTest,
	"Design2UMG.Pencil.CorpusDiagnostics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilCorpusDiagnosticsTest::RunTest(const FString&)
{
	using namespace PencilDocumentCorpus;
	FOutputDeviceRedirector Redirector;
	Redirector.EnableBacklog(true);
	FJsonValues FirstCase, SecondCase;
	const FName Category(TEXT("PencilCorpusDiagnostics"));
	const auto EmitFromWorker = [&Redirector, Category](const TCHAR* Message, ELogVerbosity::Type Verbosity)
	{
		Async(EAsyncExecution::Thread, [&Redirector, Category, Message, Verbosity]
		{
			Redirector.Serialize(Message, Verbosity, Category);
		}).Wait();
	};
	Redirector.Serialize(TEXT("before case"), ELogVerbosity::Warning, Category);
	{
		FScopedMessages Capture(FirstCase, TEXT("parse"), &Redirector);
		EmitFromWorker(TEXT("current parse warning"), ELogVerbosity::Warning);
	}
	Redirector.Serialize(TEXT("between cases"), ELogVerbosity::Warning, Category);
	{
		FScopedMessages Capture(SecondCase, TEXT("convert"), &Redirector);
		EmitFromWorker(TEXT("current conversion error"), ELogVerbosity::Error);
	}
	Redirector.Serialize(TEXT("after cases"), ELogVerbosity::Warning, Category);
	if (!TestEqual(TEXT("Only the current case warning is captured"), FirstCase.Num(), 1)
		|| !TestEqual(TEXT("Only the current case error is captured"), SecondCase.Num(), 1)) return false;
	const auto First = FirstCase[0]->AsObject();
	const auto Second = SecondCase[0]->AsObject();
	TestEqual(TEXT("Warning retains its stage"), Text(First, TEXT("stage")), FString(TEXT("parse")));
	TestEqual(TEXT("Warning retains its severity"), Text(First, TEXT("severity")), FString(TEXT("warning")));
	TestEqual(TEXT("Warning belongs to this case"), Text(First, TEXT("message")), FString(TEXT("current parse warning")));
	TestEqual(TEXT("Error retains its stage"), Text(Second, TEXT("stage")), FString(TEXT("convert")));
	TestEqual(TEXT("Error retains its severity"), Text(Second, TEXT("severity")), FString(TEXT("error")));
	TestEqual(TEXT("Error belongs to this case"), Text(Second, TEXT("message")), FString(TEXT("current conversion error")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilDocumentCorpusTest,
	"Design2UMG.Pencil.DocumentCorpus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FPencilDocumentCorpusTest::RunTest(const FString&)
{
	using namespace PencilDocumentCorpus;
	FString ManifestPath;
	if (!FParse::Value(FCommandLine::Get(), TEXT("D2UPencilCorpusManifest="), ManifestPath))
	{
		AddInfo(TEXT("Corpus not requested: supply -D2UPencilCorpusManifest and -D2UPencilCorpusOutput to record external input facts."));
		return true;
	}
	FString OutputRoot;
	if (!FParse::Value(FCommandLine::Get(), TEXT("D2UPencilCorpusOutput="), OutputRoot) || OutputRoot.IsEmpty())
	{
		AddError(TEXT("D2UPencilCorpusOutput is required for the requested corpus."));
		return false;
	}
	TArray<uint8> ManifestBytes;
	FString ManifestJson;
	TSharedPtr<FJsonObject> Manifest;
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (FFileHelper::LoadFileToArray(ManifestBytes, *ManifestPath))
		FFileHelper::BufferToString(ManifestJson, ManifestBytes.GetData(), ManifestBytes.Num());
	if (!ReadObject(ManifestJson, Manifest)
		|| !Manifest->TryGetArrayField(TEXT("files"), Entries) || Entries->IsEmpty()
		|| !IFileManager::Get().MakeDirectory(*OutputRoot, true))
	{
		AddError(TEXT("Corpus manifest/files or output directory is invalid."));
		return false;
	}

	FJsonValues Records;
	TMap<FString, int32> Outcomes;
	bool bWritten = true;
	bool bInputsIdentical = true;
	for (int32 Index = 0; Index < Entries->Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (!(*Entries)[Index]->TryGetObject(Entry))
		{
			AddError(FString::Printf(TEXT("Corpus entry %d is not an object."), Index));
			bWritten = false;
			continue;
		}
		const TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
		Record->SetNumberField(TEXT("index"), Index);
		for (const TCHAR* Key : { TEXT("local_path"), TEXT("sha256"), TEXT("version"), TEXT("sample_kind"),
			TEXT("git_blob_sha1"), TEXT("fixture_purpose"), TEXT("json_check"), TEXT("original_url"), TEXT("commit") })
			Record->SetStringField(Key, Text(*Entry, Key));
		const FString Path = Text(*Entry, TEXT("local_path"));
		const FString Version = Text(*Entry, TEXT("version"));
		const bool bUnverifiedFormat = Version == TEXT("1") || Version == TEXT("1.0") || Version == TEXT("9.9")
			|| Text(*Entry, TEXT("json_check")).Contains(TEXT("jsonc"));
		Record->SetBoolField(TEXT("declared_format_requires_separate_review"), bUnverifiedFormat);
		Record->SetBoolField(TEXT("parse_attempted"), false);
		Record->SetBoolField(TEXT("parse_succeeded"), false);
		Record->SetBoolField(TEXT("conversion_attempted"), false);
		FString Outcome = TEXT("input_error");
		TArray<uint8> Bytes;
		const bool bLoaded = FFileHelper::LoadFileToArray(Bytes, *Path);
		const FString ActualHash = bLoaded ? GitBlobHash(Bytes) : FString();
		const bool bIdentity = bLoaded && ActualHash.Equals(Text(*Entry, TEXT("git_blob_sha1")), ESearchCase::IgnoreCase);
		Record->SetStringField(TEXT("actual_git_blob_sha1"), ActualHash);
		Record->SetStringField(TEXT("identity_algorithm"), TEXT("git_blob_sha1; original SHA256 retained from manifest"));
		Record->SetBoolField(TEXT("identity_matches"), bIdentity);
		Record->SetNumberField(TEXT("actual_bytes"), Bytes.Num());
		if (!bIdentity) bInputsIdentical = false;
		if (bIdentity)
		{
			FString Json;
			FFileHelper::BufferToString(Json, Bytes.GetData(), Bytes.Num());
			TSharedPtr<FJsonObject> SourceObject;
			FJsonValues Messages;
			FSourceFacts Source;
			FJsonValues Imports;
			if (FPenParser::ParseSourceObject(Json, SourceObject))
			{
				Record->SetStringField(TEXT("raw_version"), Text(SourceObject, TEXT("version")));
				Source = InspectSource(SourceObject);
				const TSharedPtr<FJsonObject>* ImportObject = nullptr;
				if (SourceObject->TryGetObjectField(TEXT("imports"), ImportObject))
				{
					for (const auto& Import : (*ImportObject)->Values)
					{
						FString Library;
						Import.Value->TryGetString(Library);
						const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
						Item->SetStringField(TEXT("alias"), Import.Key);
						Item->SetStringField(TEXT("library"), Library);
						Imports.Add(MakeShared<FJsonValueObject>(Item));
					}
				}
			}
			Record->SetObjectField(TEXT("source_node_types"), Counts(Source.Types));
			Record->SetNumberField(TEXT("source_dynamic_objects_not_runtime_verified"), Source.DynamicObjects);
			Record->SetArrayField(TEXT("imports"), Imports);
			Record->SetStringField(TEXT("parser_entry"), TEXT("ParseDocumentFile; production source/import/resource handling"));
			FDesignDocument Document;
			bool bParsed;
			{
				FScopedMessages Capture(Messages, TEXT("parse"));
				Record->SetBoolField(TEXT("parse_attempted"), true);
				bParsed = FPenParser::ParseDocumentFile(Path, Document);
			}
			Record->SetBoolField(TEXT("parse_succeeded"), bParsed);
			Record->SetStringField(TEXT("parsed_version"), Document.Version);
			Record->SetBoolField(TEXT("parsed_version_matches_manifest"), bParsed && Document.Version == Version);
			Outcome = TEXT("parse_rejected");
			if (bParsed)
			{
				const FDesignFacts Design = InspectDesign(Document.Children, Source);
				const FDesignFacts Imported = InspectDesign(Document.SourceDocument().ImportedNodes, Source);
				Record->SetNumberField(TEXT("imported_document_root_count"), Document.SourceDocument().ImportedNodes.Num());
				Record->SetNumberField(TEXT("imported_node_count"), Imported.Nodes);
				Record->SetObjectField(TEXT("imported_node_types"), Counts(Imported.Types));
				Record->SetObjectField(TEXT("imported_source_to_ir_types"), Counts(Imported.SourceMappings));
				Record->SetArrayField(TEXT("imported_non_frame_sources_mapped_to_frame"), Imported.FrameMappings);
				Record->SetNumberField(TEXT("parsed_node_count"), Design.Nodes);
				Record->SetObjectField(TEXT("parsed_node_types"), Counts(Design.Types));
				Record->SetObjectField(TEXT("source_to_ir_types"), Counts(Design.SourceMappings));
				Record->SetArrayField(TEXT("non_frame_sources_mapped_to_frame"), Design.FrameMappings);
				FJsonValues Skipped;
				for (const FDesignSkippedNodeReport& Node : Document.ParseDiagnostics().SkippedDesignerNodes)
				{
					const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
					Item->SetStringField(TEXT("id"), Node.NodeId);
					Item->SetStringField(TEXT("source_type"), Node.NodeType);
					Item->SetStringField(TEXT("reason"), Node.Reason);
					Skipped.Add(MakeShared<FJsonValueObject>(Item));
				}
				Record->SetArrayField(TEXT("skipped_designer_nodes"), Skipped);
				FJsonValues ReferenceFailures;
				int32 Expanded = 0;
				int32 ExpandedUnknownNodes = 0;
				int32 NestedRefsRemaining = 0;
				TMap<FString, int32> ExpandedTypes;
				FJsonValues ExpandedFrameMappings;
				TArray<TSharedPtr<FDesignNode>> References = Design.References;
				References.Append(Imported.References);
				{
					FScopedMessages Capture(Messages, TEXT("reference_expansion"));
					for (const TSharedPtr<FDesignNode>& Ref : References)
					{
						const FDesignReferenceExpansionResult Result = RefExpander::ExpandRef(Ref, Document);
						if (Result.IsSuccess())
						{
							++Expanded;
							const FDesignFacts Effective = InspectDesign({ Result.ExpandedNode }, Source);
							ExpandedUnknownNodes += Effective.UnknownNodes;
							NestedRefsRemaining += Effective.References.Num();
							ExpandedFrameMappings.Append(Effective.FrameMappings);
							for (const auto& Type : Effective.Types) ExpandedTypes.FindOrAdd(Type.Key) += Type.Value;
							continue;
						}
						const TSharedRef<FJsonObject> Failure = MakeShared<FJsonObject>();
						Failure->SetStringField(TEXT("id"), Result.NodeId);
						Failure->SetStringField(TEXT("target"), Result.ReferencedNodeId);
						Failure->SetNumberField(TEXT("status"), static_cast<int32>(Result.Status));
						Failure->SetStringField(TEXT("message"), Result.Message);
						ReferenceFailures.Add(MakeShared<FJsonValueObject>(Failure));
					}
				}
				Record->SetNumberField(TEXT("references_observed"), References.Num());
				Record->SetNumberField(TEXT("references_expanded"), Expanded);
				Record->SetArrayField(TEXT("reference_failures"), ReferenceFailures);
				Record->SetObjectField(TEXT("expanded_node_types"), Counts(ExpandedTypes));
				Record->SetNumberField(TEXT("expanded_unknown_nodes"), ExpandedUnknownNodes);
				Record->SetNumberField(TEXT("nested_refs_remaining_in_expansion"), NestedRefsRemaining);
				Record->SetArrayField(TEXT("expanded_non_frame_sources_mapped_to_frame"), ExpandedFrameMappings);
				FUMGWidgetData Converted;
				{
					FScopedMessages Capture(Messages, TEXT("convert"));
					Record->SetBoolField(TEXT("conversion_attempted"), true);
					Converted = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
				}
				TMap<FString, int32> WidgetTypes;
				int32 WithSourceIdentity = 0;
				const int32 Widgets = InspectWidgets(Converted.RootWidget, WidgetTypes, WithSourceIdentity);
				Record->SetNumberField(TEXT("widget_count"), Widgets);
				Record->SetNumberField(TEXT("widgets_with_source_identity"), WithSourceIdentity);
				Record->SetObjectField(TEXT("widget_types"), Counts(WidgetTypes));
				Record->SetStringField(TEXT("conversion_error"), Converted.ConversionError);
				Record->SetNumberField(TEXT("conversion_status"), static_cast<int32>(Converted.GetConversionStatus()));
				FJsonValues Warnings;
				for (const FString& Warning : Converted.Warnings) Warnings.Add(MakeShared<FJsonValueString>(Warning));
				Record->SetArrayField(TEXT("conversion_warnings"), Warnings);
				Outcome = Converted.GetConversionStatus() == EDesign2UMGConversionStatus::Failed ? TEXT("conversion_failed")
					: Widgets == 0 ? TEXT("no_widget_output") : TEXT("static_conversion_observed");
				if (Outcome == TEXT("static_conversion_observed") && (bUnverifiedFormat || Document.Version != Version || Source.DynamicObjects > 0
					|| Design.UnknownNodes > 0 || Imported.UnknownNodes > 0 || ExpandedUnknownNodes > 0
					|| !Design.FrameMappings.IsEmpty() || !Imported.FrameMappings.IsEmpty() || !ExpandedFrameMappings.IsEmpty()
					|| !ReferenceFailures.IsEmpty() || !Skipped.IsEmpty() || !Warnings.IsEmpty() || !Messages.IsEmpty()
					|| !Text(*Entry, TEXT("fixture_purpose")).IsEmpty())) Outcome = TEXT("requires_review");
			}
			Record->SetArrayField(TEXT("diagnostics"), Messages);
		}
		Record->SetStringField(TEXT("observation_status"), Outcome);
		++Outcomes.FindOrAdd(Outcome);
		const FString ReportName = FString::Printf(TEXT("%04d-%s.json"), Index, *Text(*Entry, TEXT("sha256")).Left(12));
		Record->SetStringField(TEXT("report"), ReportName);
		if (!WriteObject(FPaths::Combine(OutputRoot, ReportName), Record))
		{
			AddError(FString::Printf(TEXT("Failed to write corpus record %d."), Index));
			bWritten = false;
		}
		const TSharedRef<FJsonObject> IndexRecord = MakeShared<FJsonObject>();
		for (const TCHAR* Key : { TEXT("index"), TEXT("local_path"), TEXT("sha256"), TEXT("version"), TEXT("sample_kind"),
			TEXT("fixture_purpose"), TEXT("observation_status"), TEXT("parse_succeeded"), TEXT("conversion_status"),
			TEXT("widget_count"), TEXT("references_observed"), TEXT("references_expanded"), TEXT("imported_node_count"), TEXT("report") })
		{
			const TSharedPtr<FJsonValue> Value = Record->TryGetField(Key);
			if (Value.IsValid()) IndexRecord->SetField(Key, Value);
		}
		Records.Add(MakeShared<FJsonValueObject>(IndexRecord));
	}
	const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetStringField(TEXT("manifest"), ManifestPath);
	Summary->SetStringField(TEXT("manifest_git_blob_sha1"), GitBlobHash(ManifestBytes));
	Summary->SetNumberField(TEXT("manifest_file_count"), Entries->Num());
	Summary->SetNumberField(TEXT("record_count"), Records.Num());
	Summary->SetBoolField(TEXT("records_complete"), bWritten && Records.Num() == Entries->Num());
	Summary->SetBoolField(TEXT("input_identities_match"), bInputsIdentical);
	Summary->SetStringField(TEXT("compatibility_verdict"), TEXT("not_assessed; inspect per-file outcomes, diagnostics and source evidence"));
	Summary->SetStringField(TEXT("scope"), TEXT("Production parse, reference expansion and UMG IR conversion; script nodes may execute during expansion; no native asset generation or rendering"));
	Summary->SetStringField(TEXT("automation_errors"), TEXT("Parser/converter logs remain visible to UE automation, including malformed fixture diagnostics"));
	Summary->SetObjectField(TEXT("outcomes"), Counts(Outcomes));
	Summary->SetArrayField(TEXT("records"), Records);
	if (!WriteObject(FPaths::Combine(OutputRoot, TEXT("summary.json")), Summary))
	{
		AddError(TEXT("Failed to write corpus summary."));
		return false;
	}
	AddInfo(FString::Printf(TEXT("Recorded %d/%d corpus files. This is not a compatibility or visual PASS."), Records.Num(), Entries->Num()));
	return bWritten && bInputsIdentical && Records.Num() == Entries->Num();
}

#endif
