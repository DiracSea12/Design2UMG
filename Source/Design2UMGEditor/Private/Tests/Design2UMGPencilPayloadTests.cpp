// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorMcpImportPayload.h"
#include "Design2UMGEditorImportRequestSetup.h"
#include "DesignJsonUtils.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignReferenceExpansion.h"
#include "Dom/JsonObject.h"
#include "ExternalServices/PenMCPBridge.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "UMGSchema.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilSavedDocumentPathTest,
	"Design2UMG.Pencil.SavedDocumentPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilSavedDocumentPathTest::RunTest(const FString&)
{
	FString FilePath;
	const FString Response = TEXT("# Current App State\n\n## Canvas Editor\n- Currently active canvas editor: `/g:/Designs/中文 界面.pen`\n- Selected nodes: No nodes selected\n");
	TestTrue(TEXT("Reads the actual active saved file path"),
		Design2UMGEditorMcpImportPayload::ParseActiveDocumentPath(Response, FilePath));
	TestEqual(TEXT("Normalizes the Pencil Windows path and preserves Unicode and spaces"),
		FilePath, FString(TEXT("g:/Designs/中文 界面.pen")));
	TestFalse(TEXT("A new unsaved document has no disk source"),
		Design2UMGEditorMcpImportPayload::ParseActiveDocumentPath(
			TEXT("- Currently active canvas editor: `untitled:Untitled-1`"), FilePath));
	TestTrue(TEXT("An unsaved document clears the preceding path"), FilePath.IsEmpty());
	TestFalse(TEXT("Does not accept partial live node payloads as a saved source"),
		Design2UMGEditorMcpImportPayload::ParseActiveDocumentPath(
			TEXT("Design2UMGDocument:{\"nodes\":[],\"variables\":{}}"), FilePath));
	TestFalse(TEXT("Rejects an incomplete active path marker"),
		Design2UMGEditorMcpImportPayload::ParseActiveDocumentPath(
			TEXT("- Currently active canvas editor: `/g:/Designs/design.pen"), FilePath));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilReadDocumentPayloadTest,
	"Design2UMG.Pencil.ReadDocumentPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilReadDocumentPayloadTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Args = Design2UMGEditorImportRequestSetup::MakePencilReadDocumentArgs();
	FString FilePath;
	TestTrue(TEXT("The execute schema requires filePath"), Args->TryGetStringField(TEXT("filePath"), FilePath));
	TestTrue(TEXT("An empty filePath reads the active in-memory document"), FilePath.IsEmpty());
	const FString Script = Args->GetStringField(TEXT("input"));
	TestTrue(TEXT("Reads subtrees with path geometry"), Script.Contains(TEXT("Get(n.id,{depth:99,includePathGeometry:true})")));
	TestFalse(TEXT("Preserves all top-level types and components"), Script.Contains(TEXT("n.type")));
	TestTrue(TEXT("Reads variables in the same execute"), Script.Contains(TEXT("GetVariables()")));

	const FString Response = TEXT(R"json(OK
## Print output
Design2UMGDocument:{"nodes":[{"id":"root","type":"group","name":"未保存界面","children":[{"id":"image","type":"rectangle","width":8,"height":8,"fill":{"type":"image","url":"images/live.png"}}]},{"id":"component","type":"rectangle","reusable":true,"width":8,"height":8}],"variables":{"variables":{"accent":{"type":"color","value":"#ff0000"}},"themes":{"mode":["light","dark"]}}}
)json");
	FString Nodes, Variables;
	if (!TestTrue(TEXT("Reads live data without a file path or complete document headers"),
		Design2UMGEditorMcpImportPayload::ParseReadDocumentResponse(Response, Nodes, Variables))) return false;
	TArray<TSharedPtr<FJsonValue>> ParsedNodes;
	if (!TestTrue(TEXT("Keeps the node array"), DesignJsonUtils::TryParseArray(Nodes, ParsedNodes))) return false;
	if (!TestEqual(TEXT("Keeps non-frame roots and components"), ParsedNodes.Num(), 2)) return false;
	TestEqual(TEXT("Keeps the live Unicode name"), ParsedNodes[0]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("未保存界面")));

	FDesignDocument Document;
	if (!TestTrue(TEXT("Parses an untitled snapshot without disk input"), FPenMCPBridge::ParseFromMCP(Nodes, Variables, Document))) return false;
	if (!TestEqual(TEXT("Keeps both top-level nodes"), Document.Children.Num(), 2)) return false;
	TestTrue(TEXT("Keeps live variables and themes"), Document.Variables.Contains(TEXT("accent")) && Document.Themes.Contains(TEXT("mode")));
	TestTrue(TEXT("An untitled snapshot needs no native path"), Document.SourceDocument().SourceFilePath.IsEmpty());
	TestTrue(TEXT("A headerless live snapshot has an unknown version"), Document.Version.IsEmpty());

	const FString SourcePath = FPaths::ConvertRelativePathToFull(TEXT("PencilMemory/unsaved.pen"));
	if (!TestTrue(TEXT("A source path only anchors resources"), FPenMCPBridge::ParseFromMCP(Nodes, Variables, Document, TEXT("root"), SourcePath))) return false;
	TestTrue(TEXT("A resource path does not supply a format version"), Document.Version.IsEmpty());
	const TSharedPtr<FDesignNode>* RootNode = Document.Children.FindByPredicate([](const TSharedPtr<FDesignNode>& Node) { return Node->Id == TEXT("root"); });
	if (!TestNotNull(TEXT("Keeps the selected live root"), RootNode)) return false;
	if (!TestEqual(TEXT("Keeps the live image node"), (*RootNode)->Children.Num(), 1)) return false;
	if (!TestEqual(TEXT("Keeps the live image fill"), (*RootNode)->Children[0]->Visual.Fills.Num(), 1)) return false;
	TestEqual(TEXT("Resolves the live image against the source directory"), (*RootNode)->Children[0]->Visual.Fills[0].Image.Url,
		FPaths::GetPath(SourcePath) / TEXT("images/live.png"));

	TestFalse(TEXT("Rejects status text without a payload"),
		Design2UMGEditorMcpImportPayload::ParseReadDocumentResponse(TEXT("OK"), Nodes, Variables));
	TestFalse(TEXT("Rejects malformed nodes"),
		Design2UMGEditorMcpImportPayload::ParseReadDocumentResponse(TEXT("Design2UMGDocument:{\"nodes\":{},\"variables\":{\"variables\":{}}}"), Nodes, Variables));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilSelectedReferencePayloadTest,
	"Design2UMG.Pencil.SelectedReferencePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilSelectedReferencePayloadTest::RunTest(const FString&)
{
	const FString Nodes = TEXT(R"json([
		{"id":"definitions","type":"frame","children":[
			{"id":"field","type":"frame","width":120,"height":30,"children":[
				{"id":"label","type":"text","content":"Live input"},{"id":"slot","type":"frame"}]},
			{"id":"alias","type":"ref","ref":"field"},
			{"id":"detail","type":"frame","children":[{"id":"body","type":"text","content":"Default"}]},
			{"id":"leaf","type":"text","content":"Override content"}]},
		{"id":"page","type":"frame","children":[{"id":"input","type":"ref","ref":"alias","descendants":{
			"slot":{"children":[{"id":"override-ref","type":"ref","ref":"detail","descendants":{
				"body":{"id":"replacement","type":"ref","ref":"leaf"}}}]}}}]},
		{"id":"unused","type":"text","content":"Unrelated font request","fontFamily":"UnusedFixtureFont"}
	])json");
	FDesignDocument Document;
	if (!TestTrue(TEXT("The MCP selection keeps ordinary reference definitions"),
		FPenMCPBridge::ParseFromMCP(Nodes, TEXT("{}"), Document, TEXT("page")))) return false;

	auto CheckSelection = [this](const FDesignDocument& Selected)
	{
		if (!TestEqual(TEXT("Only the selected page is an output root"), Selected.Children.Num(), 1)) return;
		TestEqual(TEXT("The page is selected"), Selected.Children[0]->Id, FString(TEXT("page")));
		TMap<FString, TSharedPtr<FDesignNode>> Targets;
		Selected.BuildReferenceNodeIndex(Targets);
		const TSharedPtr<FDesignNode> Field = Targets.FindRef(TEXT("field"));
		if (!TestTrue(TEXT("A nested ordinary target remains indexable"), Field.IsValid())) return;
		TestFalse(TEXT("An ordinary target is not made reusable"), Field->bReusable);
		TestFalse(TEXT("The unrelated font node is excluded from the retained trees"), Targets.Contains(TEXT("unused")));
		TestFalse(TEXT("The unreferenced definition container is not retained"), Targets.Contains(TEXT("definitions")));
		TestEqual(TEXT("Only the four referenced definitions enter subsequent import work"), Selected.SourceDocument().ImportedNodes.Num(), 4);
		TestTrue(TEXT("Override subtrees and typed replacements keep their reference targets"),
			Targets.Contains(TEXT("detail")) && Targets.Contains(TEXT("leaf")));
		if (!TestEqual(TEXT("The selected page keeps its ref"), Selected.Children[0]->Children.Num(), 1)) return;
		const FDesignReferenceExpansionResult Expansion = RefExpander::ExpandRef(Selected.Children[0]->Children[0], Selected);
		if (!TestTrue(TEXT("The transitive ordinary reference expands"), Expansion.IsSuccess())) return;
		if (!TestEqual(TEXT("The input keeps its target content and slot"), Expansion.ExpandedNode->Children.Num(), 2)) return;
		TestEqual(TEXT("The input text survives"), Expansion.ExpandedNode->Children[0]->Text.Content, FString(TEXT("Live input")));
		const auto& Slot = Expansion.ExpandedNode->Children[1];
		if (!TestEqual(TEXT("The override supplies the slot subtree"), Slot->Children.Num(), 1)) return;
		const auto Detail = RefExpander::ExpandRef(Slot->Children[0], Selected);
		if (!TestTrue(TEXT("The ref authored in an override subtree expands"), Detail.IsSuccess())) return;
		if (!TestEqual(TEXT("The detail has its replacement body"), Detail.ExpandedNode->Children.Num(), 1)) return;
		const auto Leaf = RefExpander::ExpandRef(Detail.ExpandedNode->Children[0], Selected);
		if (!TestTrue(TEXT("The typed descendant replacement ref expands"), Leaf.IsSuccess())) return;
		TestEqual(TEXT("The replacement target content survives"), Leaf.ExpandedNode->Text.Content, FString(TEXT("Override content")));
	};
	CheckSelection(Document);
	TestTrue(TEXT("The MCP version remains unknown"), Document.Version.IsEmpty());

	TArray<TSharedPtr<FJsonValue>> Children;
	if (!TestTrue(TEXT("The local document children parse"), DesignJsonUtils::TryParseArray(Nodes, Children))) return false;
	auto Source = MakeShared<FJsonObject>();
	Source->SetStringField(TEXT("version"), TEXT("2.19"));
	Source->SetArrayField(TEXT("children"), Children);
	TSharedPtr<FJsonObject> Filtered;
	if (!TestTrue(TEXT("The local selection keeps its definition trees"), FPenMCPBridge::BuildFilteredDocumentObject(Source, Filtered, TEXT("page")))) return false;
	if (!TestTrue(TEXT("The local selection parses"), FPenParser::ParseDocumentObject(Filtered, Document))) return false;
	TMap<FString, TSharedPtr<FDesignNode>> OriginalTargets;
	Document.BuildReferenceNodeIndex(OriginalTargets);
	const auto OriginalField = OriginalTargets.FindRef(TEXT("field"));
	FPenMCPBridge::PromoteSelectedNodeToRenderRoot(Document, TEXT("page"));
	CheckSelection(Document);
	TestTrue(TEXT("Selection does not apply instance overrides to the definition tree"),
		OriginalField.IsValid() && OriginalField->Children[1]->Children.IsEmpty());
	TestEqual(TEXT("The actual file version is preserved"), Document.Version, FString(TEXT("2.19")));
	TestEqual(TEXT("The caller's source tree is unchanged"), Source->GetArrayField(TEXT("children")).Num(), 3);
	const auto& SourceField = Source->GetArrayField(TEXT("children"))[0]->AsObject()->GetArrayField(TEXT("children"))[0]->AsObject();
	TestEqual(TEXT("Dependency selection leaves the original slot unchanged"),
		SourceField->GetArrayField(TEXT("children"))[1]->AsObject()->GetStringField(TEXT("id")), FString(TEXT("slot")));
	Source->SetStringField(TEXT("version"), TEXT("1.0"));
	if (!TestTrue(TEXT("A file declaring 1.0 still parses"), FPenParser::ParseDocumentObject(Source, Document))) return false;
	TestEqual(TEXT("A declared 1.0 is preserved"), Document.Version, FString(TEXT("1.0")));

	if (!TestTrue(TEXT("A nested target can be selected"), FPenMCPBridge::ParseFromMCP(Nodes, TEXT("{}"), Document, TEXT("field")))) return false;
	if (!TestEqual(TEXT("Only the nested target is an output root"), Document.Children.Num(), 1)) return false;
	TestEqual(TEXT("The nested target is promoted"), Document.Children[0]->Id, FString(TEXT("field")));
	const FString ScriptPath = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("d2u-selected-script-"), TEXT(".js"));
	struct FScriptScope { FString Path; ~FScriptScope() { IFileManager::Get().Delete(*Path); } } ScriptCleanup{ScriptPath};
	if (!TestTrue(TEXT("Selection fixture script saved"), FFileHelper::SaveStringToFile(
		TEXT("/**\n * @schema 2.11\n * @input target: ref = \"unused\"\n * @input label: string = \"unused\"\n */\nreturn [{type:'ref',ref:pencil.input.target}];"),
		*ScriptPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	TArray<TSharedPtr<FJsonValue>> ScriptRoots;
	if (!TestTrue(TEXT("Script selection fixture parses"), DesignJsonUtils::TryParseArray(TEXT(R"json([
		{"id":"page","type":"frame","width":60,"height":40,"children":[
			{"id":"script","type":"script","width":60,"height":40,"inputs":{"target":"external"}}]},
		{"id":"external","type":"rectangle","name":"External content","width":10,"height":12,"fill":"#ffffff"},
		{"id":"unused","type":"rectangle","width":20,"height":20}
	])json"), ScriptRoots))) return false;
	ScriptRoots[0]->AsObject()->GetArrayField(TEXT("children"))[0]->AsObject()->SetStringField(TEXT("scriptUri"), ScriptPath);
	auto ScriptDocumentJSON = MakeShared<FJsonObject>();
	ScriptDocumentJSON->SetStringField(TEXT("version"), TEXT("2.21"));
	ScriptDocumentJSON->SetArrayField(TEXT("children"), ScriptRoots);
	if (!TestTrue(TEXT("Actual selected-document preparation succeeds"), FPenMCPBridge::BuildFilteredDocumentObject(ScriptDocumentJSON, Filtered, TEXT("page")))) return false;
	if (!TestTrue(TEXT("Script selection uses the real parser"), FPenParser::ParseDocumentObject(Filtered, Document))) return false;
	FPenMCPBridge::PromoteSelectedNodeToRenderRoot(Document, TEXT("page"));
	TestEqual(TEXT("Script selection emits one page"), Document.Children.Num(), 1);
	TMap<FString, TSharedPtr<FDesignNode>> ScriptTargets; Document.BuildReferenceNodeIndex(ScriptTargets);
	TestTrue(TEXT("An ordinary external script ref is retained"), ScriptTargets.Contains(TEXT("external")));
	TestFalse(TEXT("String inputs and overridden defaults do not retain a page"), ScriptTargets.Contains(TEXT("unused")));
	if (ScriptTargets.Contains(TEXT("external"))) TestFalse(TEXT("Script dependency stays ordinary"), ScriptTargets[TEXT("external")]->bReusable);
	FDesignDocument DirectScriptDocument = Document;
	const auto Component = ScriptRoots[0]->AsObject()->GetArrayField(TEXT("children"))[0]->AsObject();
	Component->SetBoolField(TEXT("reusable"), true);
	auto Instance = MakeShared<FJsonObject>(); Instance->SetStringField(TEXT("id"), TEXT("script-instance"));
	Instance->SetStringField(TEXT("type"), TEXT("ref")); Instance->SetStringField(TEXT("ref"), TEXT("script"));
	auto InstanceInputs = MakeShared<FJsonObject>(); InstanceInputs->SetStringField(TEXT("target"), TEXT("instance-target"));
	Instance->SetObjectField(TEXT("inputs"), InstanceInputs);
	ScriptRoots[0]->AsObject()->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(Instance)});
	auto InstanceTarget = MakeShared<FJsonObject>(); InstanceTarget->SetStringField(TEXT("id"), TEXT("instance-target"));
	InstanceTarget->SetStringField(TEXT("type"), TEXT("rectangle")); InstanceTarget->SetNumberField(TEXT("width"), 10); InstanceTarget->SetNumberField(TEXT("height"), 10);
	ScriptRoots.Add(MakeShared<FJsonValueObject>(Component)); ScriptRoots.Add(MakeShared<FJsonValueObject>(InstanceTarget));
	ScriptDocumentJSON->SetArrayField(TEXT("children"), ScriptRoots);
	if (!TestTrue(TEXT("Script instances parse"), FPenParser::ParseDocumentObject(ScriptDocumentJSON, Document))) return false;
	FPenMCPBridge::PromoteSelectedNodeToRenderRoot(Document, TEXT("page"));
	ScriptTargets.Reset(); Document.BuildReferenceNodeIndex(ScriptTargets);
	TestTrue(TEXT("Effective script instance inputs retain their ordinary external target"), ScriptTargets.Contains(TEXT("instance-target")));
	TestEqual(TEXT("Instance dependency retention still emits only one page"), Document.Children.Num(), 1);

	TSharedPtr<FJsonObject> ThemedSource;
	if (!TestTrue(TEXT("Transitive themed script fixture parses"), DesignJsonUtils::TryParseObject(TEXT(R"json({
		"version":"2.21","themes":{"Mode":["A","B"]},
		"variables":{"destination":{"type":"string","value":[
			{"value":"targetA","theme":{"Mode":"A"}}, {"value":"targetB","theme":{"Mode":"B"}}]}},
		"children":[
			{"id":"page","type":"frame","width":60,"height":40,"children":[
				{"id":"scriptA","type":"script","width":20,"height":20,"theme":{"Mode":"A"},"inputs":{"target":"shared"}},
				{"id":"scriptB","type":"script","width":20,"height":20,"theme":{"Mode":"B"},"inputs":{"target":"shared"}}]},
			{"id":"shared","type":"script","width":20,"height":20,"inputs":{"target":"$destination"}},
			{"id":"targetA","type":"rectangle","name":"Theme target A","width":10,"height":10,"fill":"#ffffff"},
			{"id":"targetB","type":"rectangle","name":"Theme target B","width":10,"height":10,"fill":"#ffffff"},
			{"id":"unused","type":"rectangle","width":20,"height":20}
		]
	})json"), ThemedSource))) return false;
	const auto& ThemedRoots = ThemedSource->GetArrayField(TEXT("children"));
	for (const auto& Script : ThemedRoots[0]->AsObject()->GetArrayField(TEXT("children")))
		Script->AsObject()->SetStringField(TEXT("scriptUri"), ScriptPath);
	ThemedRoots[1]->AsObject()->SetStringField(TEXT("scriptUri"), ScriptPath);
	if (!TestTrue(TEXT("Themed selection uses actual document preparation"), FPenMCPBridge::BuildFilteredDocumentObject(ThemedSource, Filtered, TEXT("page")))) return false;
	FDesignDocument ThemedDocument;
	if (!TestTrue(TEXT("Themed selection uses the production parser"), FPenParser::ParseDocumentObject(Filtered, ThemedDocument))) return false;
	FPenMCPBridge::PromoteSelectedNodeToRenderRoot(ThemedDocument, TEXT("page"));
	ScriptTargets.Reset(); ThemedDocument.BuildReferenceNodeIndex(ScriptTargets);
	TestEqual(TEXT("Themed dependency retention outputs only the selected page"), ThemedDocument.Children.Num(), 1);
	TestTrue(TEXT("Shared typed script dependency retains both effective theme targets"),
		ScriptTargets.Contains(TEXT("shared")) && ScriptTargets.Contains(TEXT("targetA")) && ScriptTargets.Contains(TEXT("targetB")));
	TestFalse(TEXT("Themed retention does not keep unrelated pages"), ScriptTargets.Contains(TEXT("unused")));

#if PLATFORM_WINDOWS
	const auto ScriptResult = FDesign2UMGConverter::Convert(DirectScriptDocument, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Selected script converts through the actual ref expansion"), ScriptResult.ConversionError.IsEmpty());
	TArray<const FUMGWidgetNode*> Pending{&ScriptResult.RootWidget};
	bool SawExternal = false;
	while (!Pending.IsEmpty())
	{
		const auto* Widget = Pending.Last(); Pending.RemoveAt(Pending.Num() - 1);
		SawExternal |= Widget->Name == TEXT("External content");
		for (const auto& Child : Widget->Children) Pending.Add(&Child);
	}
	TestTrue(TEXT("Retained content reaches the selected widget tree"), SawExternal);
	const auto ThemedResult = FDesign2UMGConverter::Convert(ThemedDocument, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Both transitive themed script dependencies convert"), ThemedResult.ConversionError.IsEmpty());
	Pending = {&ThemedResult.RootWidget};
	bool SawTargetA = false, SawTargetB = false;
	while (!Pending.IsEmpty())
	{
		const auto* Widget = Pending.Last(); Pending.RemoveAt(Pending.Num() - 1);
		SawTargetA |= Widget->Name == TEXT("Theme target A");
		SawTargetB |= Widget->Name == TEXT("Theme target B");
		for (const auto& Child : Widget->Children) Pending.Add(&Child);
	}
	TestTrue(TEXT("Both actual theme targets reach the converted selected tree"), SawTargetA && SawTargetB);
#endif

	return true;
}
