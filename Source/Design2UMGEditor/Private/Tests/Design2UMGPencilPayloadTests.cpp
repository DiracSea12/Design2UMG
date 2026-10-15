// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportRequestSetup.h"
#include "Design2UMGEditorMcpImportPayload.h"
#include "DesignJsonUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilReadDocumentPayloadTest,
	"Design2UMG.Pencil.ReadDocumentPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilReadDocumentPayloadTest::RunTest(const FString&)
{
	const TSharedPtr<FJsonObject> Args = Design2UMGEditorImportRequestSetup::MakePencilReadDocumentArgs();
	TestEqual(TEXT("Remote import reads the active document"), Args->GetStringField(TEXT("filePath")), FString());
	const FString Script = Args->GetStringField(TEXT("input"));
	TestTrue(TEXT("Reads complete nodes with path geometry"), Script.Contains(TEXT("Get(n.id,{depth:99,includePathGeometry:true})")));
	TestTrue(TEXT("Reads variables in the same document operation"), Script.Contains(TEXT("GetVariables()")));

	FString Nodes, Variables;
	const FString Response = TEXT("OK\n\n## Print output\nDesign2UMGDocument:{\"nodes\":[{\"id\":\"root\",\"type\":\"frame\",\"name\":\"中文界面\"}],\"variables\":{\"variables\":{\"accent\":{\"type\":\"color\",\"value\":\"#ff0000\"}}}}\n");
	if (!TestTrue(TEXT("Reads the execute Print payload without parsing its status prose"),
		Design2UMGEditorMcpImportPayload::ParseReadDocumentResponse(Response, Nodes, Variables))) return false;
	TArray<TSharedPtr<FJsonValue>> ParsedNodes;
	TSharedPtr<FJsonObject> ParsedVariables;
	if (!TestTrue(TEXT("Node payload remains an array"), DesignJsonUtils::TryParseArray(Nodes, ParsedNodes))) return false;
	TestEqual(TEXT("Unicode name survives extraction"), ParsedNodes[0]->AsObject()->GetStringField(TEXT("name")), FString(TEXT("中文界面")));
	if (!TestTrue(TEXT("Variable payload remains an object"), DesignJsonUtils::TryParseObject(Variables, ParsedVariables))) return false;
	TestTrue(TEXT("Keeps named variables"), ParsedVariables->GetObjectField(TEXT("variables"))->HasField(TEXT("accent")));
	TestFalse(TEXT("Rejects a response with no import payload"),
		Design2UMGEditorMcpImportPayload::ParseReadDocumentResponse(TEXT("OK"), Nodes, Variables));
	TestFalse(TEXT("Does not import incomplete variable data"),
		Design2UMGEditorMcpImportPayload::ParseReadDocumentResponse(TEXT("Design2UMGDocument:{\"nodes\":[]}"), Nodes, Variables));
	TestFalse(TEXT("Rejects malformed node data"),
		Design2UMGEditorMcpImportPayload::ParseReadDocumentResponse(TEXT("Design2UMGDocument:{\"nodes\":{},\"variables\":{\"variables\":{}}}"), Nodes, Variables));
	return true;
}
