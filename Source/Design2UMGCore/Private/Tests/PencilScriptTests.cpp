// Copyright (c) 2026, Diracsea12. SPDX-License-Identifier: MIT

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "PencilScript.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignVariableResolver.h"
#include "Dom/JsonObject.h"
#include "RefExpander.h"
#include "PenParser.h"
#include "Design2UMGConverter.h"
#include "Design2UMGGeneratedNameSuffixes.h"
#include "UMGSchema.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilScriptContractTest, "Design2UMG.Pencil.Script.Contracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilScriptContractTest::RunTest(const FString& Parameters)
{
	const FString Script = TEXT("/**\n * @schema 2.11\n * @input n: number\n * @input label: string\n * @input online: boolean\n * @input color: color\n * @input target: ref\n * @input mode: enum(\"grid\", \"stack\")\n */\nreturn [];");
	FDesignVariableResolver Resolver;
	TSharedPtr<FJsonObject> Inputs;
	FString Error;
	TestTrue(TEXT("Six official input defaults"), PencilScript::BuildInputs(Script, MakeShared<FJsonObject>(), Resolver, Inputs, Error));
	if (!Inputs.IsValid()) return false;
	TestEqual(TEXT("Number default"), Inputs->GetNumberField(TEXT("n")), 0.0);
	TestEqual(TEXT("String default"), Inputs->GetStringField(TEXT("label")), FString());
	TestFalse(TEXT("Boolean default"), Inputs->GetBoolField(TEXT("online")));
	TestEqual(TEXT("Color default"), Inputs->GetStringField(TEXT("color")), FString(TEXT("#000000")));
	TestEqual(TEXT("Ref default"), Inputs->GetStringField(TEXT("target")), FString());
	TestEqual(TEXT("Enum default"), Inputs->GetStringField(TEXT("mode")), FString(TEXT("grid")));
	auto LiteralOverride = MakeShared<FJsonObject>();
	LiteralOverride->SetStringField(TEXT("label"), TEXT("\\$price"));
	TestTrue(TEXT("Escaped dollar input remains literal, not an unresolved variable"), PencilScript::BuildInputs(Script, LiteralOverride, Resolver, Inputs, Error));
	if (!Inputs.IsValid()) return false;
	TestEqual(TEXT("Official input loading removes one dollar escape"), Inputs->GetStringField(TEXT("label")), FString(TEXT("$price")));
	auto NumberOverride = MakeShared<FJsonObject>(); NumberOverride->SetNumberField(TEXT("n"), 99);
	TestTrue(TEXT("Official number bound grammar"), PencilScript::BuildInputs(TEXT("/**\n * @schema 2.11\n * @input n: number(min=2, max=8) = 3\n */\nreturn [];"), NumberOverride, Resolver, Inputs, Error));
	TestEqual(TEXT("Input is clamped to the declared maximum"), Inputs->GetNumberField(TEXT("n")), 8.0);
	TestFalse(TEXT("Root document version is not the script API version"), PencilScript::BuildInputs(TEXT("/** @schema 2.21 */\nreturn [];"), nullptr, Resolver, Inputs, Error));
	TArray<FString> References;
	auto ReferenceOverride = MakeShared<FJsonObject>(); ReferenceOverride->SetStringField(TEXT("target"), TEXT("actual"));
	TestTrue(TEXT("Typed ref collection uses effective inputs"), PencilScript::BuildInputs(
		TEXT("/**\n * @schema 2.11\n * @input target: ref = \"default\"\n * @input label: string = \"ordinary\"\n */"),
		ReferenceOverride, Resolver, Inputs, Error, &References));
	TestEqual(TEXT("Only the typed effective reference is collected"), References.Num(), 1);
	if (References.Num() == 1) TestEqual(TEXT("Override replaces default dependency"), References[0], FString(TEXT("actual")));
	TArray<TSharedPtr<FDesignNode>> Nodes;
	const FString JSON = TEXT(R"([{"type":"frame","id":"component","reusable":true,"width":10,"height":10,"children":[{"type":"rectangle","id":"leaf","width":3,"height":4,"fill":"#ff0000"}]},{"type":"ref","ref":"component","descendants":{"leaf":{"opacity":0.5}}},{"type":"text","content":"generated"}])");
	TestTrue(TEXT("Generated nodes use production parser"), PencilScript::ParseNodes(JSON, TEXT("scope-one"), FString(), Resolver, Nodes, Error));
	if (Nodes.Num() != 3) return false;
	TestEqual(TEXT("Internal ref follows stable component ID"), Nodes[1]->Ref, Nodes[0]->Id);
	FDesignDocument Generated;
	Generated.Children = Nodes;
	const FDesignReferenceExpansionResult Expanded = RefExpander::ExpandRef(Nodes[1], Generated);
	TestTrue(TEXT("Generated internal reference really expands"), Expanded.IsSuccess());
	TArray<TSharedPtr<FDesignNode>> Again;
	TestTrue(TEXT("Stable parse repeated"), PencilScript::ParseNodes(JSON, TEXT("scope-one"), FString(), Resolver, Again, Error));
	TestEqual(TEXT("Missing ID deterministic"), Again[2]->Id, Nodes[2]->Id);
	TArray<TSharedPtr<FDesignNode>> OtherScope;
	PencilScript::ParseNodes(JSON, TEXT("scope-two"), FString(), Resolver, OtherScope, Error);
	TestNotEqual(TEXT("Different instance scopes remain distinct"), OtherScope[0]->Id, Nodes[0]->Id);
	const FString OverridesJSON = TEXT(R"json([
		{"type":"frame","id":"base","reusable":true,"children":[{"type":"frame","id":"slot"},{"type":"rectangle","id":"remove"}]},
		{"type":"rectangle","id":"paint","reusable":true,"width":3,"height":4},
		{"type":"ref","id":"instance","ref":"base","descendants":{
			"slot":{"children":[{"type":"ref","id":"inserted","ref":"paint","name":"paint","metadata":{"ref":"paint","id":"inserted"}}]},
			"remove":{"type":"ref","id":"replacement","ref":"paint"}}}
	])json");
	if (!TestTrue(TEXT("Override nodes parse through production parser"), PencilScript::ParseNodes(OverridesJSON, TEXT("instance-one"), FString(), Resolver, Nodes, Error))) return false;
	if (!TestEqual(TEXT("Override fixture roots"), Nodes.Num(), 3)) return false;
	Generated.Children = Nodes;
	const auto OverrideExpansion = RefExpander::ExpandRef(Nodes[2], Generated);
	if (!TestTrue(TEXT("Scoped descendant paths expand"), OverrideExpansion.IsSuccess())) return false;
	const auto& Children = OverrideExpansion.ExpandedNode->Children;
	if (!TestEqual(TEXT("Both descendants survive"), Children.Num(), 2)) return false;
	if (!TestEqual(TEXT("Patch children survive"), Children[0]->Children.Num(), 1)) return false;
	const auto Inserted = Children[0]->Children[0];
	const auto Replacement = Children[1];
	TestEqual(TEXT("Patch child ref uses local scoped target"), Inserted->Ref, Nodes[1]->Id);
	TestEqual(TEXT("Replacement ref uses local scoped target"), Replacement->Ref, Nodes[1]->Id);
	TestNotEqual(TEXT("Inserted ID is scoped"), Inserted->Id, FString(TEXT("inserted")));
	TestNotEqual(TEXT("Replacement ID is scoped"), Replacement->Id, FString(TEXT("replacement")));
	TestEqual(TEXT("An ordinary string remains unchanged"), Inserted->Name, FString(TEXT("paint")));
	const auto Metadata = Inserted->Source().Pencil.Properties->GetObjectField(TEXT("metadata"));
	TestEqual(TEXT("Metadata ref remains source text"), Metadata->GetStringField(TEXT("ref")), FString(TEXT("paint")));
	TestEqual(TEXT("Metadata ID remains source text"), Metadata->GetStringField(TEXT("id")), FString(TEXT("inserted")));
	TestTrue(TEXT("The inserted reference really expands"), RefExpander::ExpandRef(Inserted, Generated).IsSuccess());
	TestTrue(TEXT("The replacement reference really expands"), RefExpander::ExpandRef(Replacement, Generated).IsSuccess());
	TArray<TSharedPtr<FDesignNode>> ScopedOverrides;
	if (!TestTrue(TEXT("Another instance parses"), PencilScript::ParseNodes(OverridesJSON, TEXT("instance-two"), FString(), Resolver, ScopedOverrides, Error))) return false;
	FDesignDocument OtherDocument; OtherDocument.Children = ScopedOverrides;
	const auto OtherExpansion = RefExpander::ExpandRef(ScopedOverrides[2], OtherDocument);
	if (!TestTrue(TEXT("Other instance expands"), OtherExpansion.IsSuccess())) return false;
	TestNotEqual(TEXT("Replacement IDs differ across instances"), OtherExpansion.ExpandedNode->Children[1]->Id, Replacement->Id);
	TestFalse(TEXT("Invalid return is explicit error"), PencilScript::ParseNodes(TEXT("{}"), TEXT("scope"), FString(), Resolver, Again, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilScriptConversionTest, "Design2UMG.Pencil.Script.Conversion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilScriptConversionTest::RunTest(const FString& Parameters)
{
#if !PLATFORM_WINDOWS
	AddInfo(TEXT("Windows script worker test is not exercised on this platform.")); return true;
#else
	const FString SourcePath = FPaths::CreateTempFilename(FPlatformProcess::UserTempDir(), TEXT("d2u-script-"), TEXT(".js"));
	struct FSourceScope { FString Path; ~FSourceScope() { IFileManager::Get().Delete(*Path); } } Cleanup{SourcePath};
	const FString Source = TEXT("/**\n * @schema 2.11\n * @input n: number = 1\n * @input caption: string = \"default\"\n */\nreturn [{type:'rectangle',name:pencil.input.caption,x:7,y:11,width:pencil.width,height:pencil.input.n+7,fill:'#ffffff'}];");
	if (!TestTrue(TEXT("Own script fixture saved"), FFileHelper::SaveStringToFile(Source, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))) return false;
	auto ScriptNode = MakeShared<FJsonObject>();
	ScriptNode->SetStringField(TEXT("id"), TEXT("component")); ScriptNode->SetStringField(TEXT("type"), TEXT("script"));
	ScriptNode->SetBoolField(TEXT("reusable"), true); ScriptNode->SetStringField(TEXT("scriptUri"), SourcePath);
	ScriptNode->SetNumberField(TEXT("width"), 40); ScriptNode->SetNumberField(TEXT("height"), 50);
	auto Inputs = MakeShared<FJsonObject>(); Inputs->SetStringField(TEXT("n"), TEXT("$size")); Inputs->SetStringField(TEXT("caption"), TEXT("keep"));
	ScriptNode->SetObjectField(TEXT("inputs"), Inputs);
	auto A = MakeShared<FJsonObject>(); A->SetStringField(TEXT("id"), TEXT("a")); A->SetStringField(TEXT("type"), TEXT("ref")); A->SetStringField(TEXT("ref"), TEXT("component")); A->SetNumberField(TEXT("width"), 80);
	auto Override = MakeShared<FJsonObject>(); Override->SetNumberField(TEXT("n"), 9); A->SetObjectField(TEXT("inputs"), Override);
	auto B = MakeShared<FJsonObject>(); B->SetStringField(TEXT("id"), TEXT("b")); B->SetStringField(TEXT("type"), TEXT("ref")); B->SetStringField(TEXT("ref"), TEXT("component")); B->SetNumberField(TEXT("width"), 120);
	auto DocumentJSON = MakeShared<FJsonObject>(); DocumentJSON->SetStringField(TEXT("version"), TEXT("2.21"));
	DocumentJSON->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(ScriptNode), MakeShared<FJsonValueObject>(A), MakeShared<FJsonValueObject>(B)});
	auto Variable = MakeShared<FJsonObject>(); Variable->SetStringField(TEXT("type"), TEXT("number")); Variable->SetNumberField(TEXT("value"), 3);
	auto Variables = MakeShared<FJsonObject>(); Variables->SetObjectField(TEXT("size"), Variable); DocumentJSON->SetObjectField(TEXT("variables"), Variables);
	FDesignDocument Document;
	if (!TestTrue(TEXT("Real parser accepts script and refs"), FPenParser::ParseDocumentObject(DocumentJSON, Document))) return false;
	TestTrue(TEXT("Script output uses local canvas coordinates, not implicit flow layout"), Document.Children[0]->LayoutData.Layout == EDesignLayout::None);
	const auto Result = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(*Result.ConversionError, Result.ConversionError.IsEmpty());
	TArray<const FUMGWidgetNode*> Pending{&Result.RootWidget}; TSet<FString> GeneratedIds; bool SawA = false, SawB = false, SlotsMatchBounds = true;
	while (!Pending.IsEmpty())
	{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		const auto* Widget = Pending.Pop(false);
#else
		const auto* Widget = Pending.Pop(EAllowShrinking::No);
#endif
		if (Widget->Name == TEXT("keep"))
		{
			TestFalse(TEXT("Generated instance IDs do not collide"), GeneratedIds.Contains(Widget->Id)); GeneratedIds.Add(Widget->Id);
			SawA |= FMath::IsNearlyEqual(Widget->Bounds.W, 80.0f) && FMath::IsNearlyEqual(Widget->Bounds.H, 16.0f);
			SawB |= FMath::IsNearlyEqual(Widget->Bounds.W, 120.0f) && FMath::IsNearlyEqual(Widget->Bounds.H, 10.0f);
			SlotsMatchBounds &= FMath::IsNearlyEqual(Widget->Bounds.X, 7.0f) && FMath::IsNearlyEqual(Widget->Bounds.Y, 11.0f)
				&& FMath::IsNearlyEqual(Widget->Slot.Offsets.Left, 7.0f) && FMath::IsNearlyEqual(Widget->Slot.Offsets.Top, 11.0f)
				&& FMath::IsNearlyEqual(Widget->Slot.Offsets.Right, Widget->Bounds.W) && FMath::IsNearlyEqual(Widget->Slot.Offsets.Bottom, Widget->Bounds.H);
		}
		for (const auto& Child : Widget->Children) Pending.Add(&Child);
	}
	TestTrue(TEXT("Ref dimensions and per-input override reach native nodes"), SawA);
	TestTrue(TEXT("Document variable and inherited input reach other instance"), SawB);
	TestTrue(TEXT("Generated child coordinates and dimensions reach native Canvas slots"), SlotsMatchBounds);
	auto OverflowInstance = MakeShared<FJsonObject>(); OverflowInstance->Values = A->Values;
	OverflowInstance->SetNumberField(TEXT("x"), -10);
	auto SlotSurface = MakeShared<FJsonObject>(); SlotSurface->SetStringField(TEXT("id"), TEXT("slot-surface"));
	SlotSurface->SetStringField(TEXT("type"), TEXT("frame")); SlotSurface->SetBoolField(TEXT("reusable"), true);
	SlotSurface->SetStringField(TEXT("layout"), TEXT("none")); SlotSurface->SetArrayField(TEXT("slot"), {});
	SlotSurface->SetNumberField(TEXT("width"), 40); SlotSurface->SetNumberField(TEXT("height"), 50);
	auto SlotInstance = MakeShared<FJsonObject>(); SlotInstance->SetStringField(TEXT("id"), TEXT("slot-instance"));
	SlotInstance->SetStringField(TEXT("type"), TEXT("ref")); SlotInstance->SetStringField(TEXT("ref"), TEXT("slot-surface"));
	SlotInstance->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(OverflowInstance)});
	auto OverflowRoot = MakeShared<FJsonObject>(); OverflowRoot->SetStringField(TEXT("id"), TEXT("overflow-root"));
	OverflowRoot->SetStringField(TEXT("type"), TEXT("frame")); OverflowRoot->SetStringField(TEXT("layout"), TEXT("none"));
	OverflowRoot->SetNumberField(TEXT("width"), 40); OverflowRoot->SetNumberField(TEXT("height"), 50);
	OverflowRoot->SetStringField(TEXT("stroke"), TEXT("#ffffff"));
	auto StrokeWidth = MakeShared<FJsonObject>(); StrokeWidth->SetNumberField(TEXT("bottom"), 1);
	OverflowRoot->SetObjectField(TEXT("strokeWidth"), StrokeWidth); OverflowRoot->SetStringField(TEXT("strokeAlignment"), TEXT("inner"));
	OverflowRoot->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(SlotInstance)});
	auto OverflowJSON = MakeShared<FJsonObject>(); OverflowJSON->Values = DocumentJSON->Values;
	OverflowJSON->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(ScriptNode), MakeShared<FJsonValueObject>(OverflowRoot), MakeShared<FJsonValueObject>(SlotSurface)});
	FDesignDocument OverflowDocument;
	if (!TestTrue(TEXT("Nested script overflow fixture uses the production parser"), FPenParser::ParseDocumentObject(OverflowJSON, OverflowDocument))) return false;
	const auto& ParsedOverflowRoot = OverflowDocument.Children[1];
	const auto SlotExpansion = RefExpander::ExpandRef(ParsedOverflowRoot->Children[0], OverflowDocument);
	if (!TestTrue(TEXT("Slot override expands with its script instance"), SlotExpansion.IsSuccess() && SlotExpansion.ExpandedNode->Children.Num() == 1)) return false;
	if (!TestTrue(TEXT("Overflow fixture has None layout and an authored negative ref position"),
		ParsedOverflowRoot->LayoutData.Layout == EDesignLayout::None
		&& SlotExpansion.ExpandedNode->Children[0]->Transform.HasExplicitX()
		&& SlotExpansion.ExpandedNode->Children[0]->Transform.X == -10.0f)) return false;
	int32 OverflowPreparations = 0;
	const auto Overflow = FDesign2UMGConverter::Convert(OverflowDocument, FDesign2UMGConversionSettings::Defaults(),
		[&OverflowPreparations](const FDesignDocument&, TArray<FString>&, FString&, FDesign2UMGConverter::EFontPreparation Preparation)
		{ if (Preparation == FDesign2UMGConverter::EFontPreparation::ImportSources) ++OverflowPreparations; return true; });
	TestTrue(TEXT("Nested script ref overflow converts"), Overflow.ConversionError.IsEmpty());
	Pending = {&Overflow.RootWidget};
	bool HasParentBottomStroke = false;
	while (!Pending.IsEmpty())
	{
		const auto* Widget = Pending.Last(); Pending.RemoveAt(Pending.Num() - 1);
		HasParentBottomStroke |= Widget->Id == ParsedOverflowRoot->Id + Design2UMGGeneratedNameSuffixes::StrokeBottom();
		for (const auto& Child : Widget->Children) Pending.Add(&Child);
	}
	TestTrue(TEXT("Expanded slot child does not change the parent's explicit stroke decision"), HasParentBottomStroke);
	const bool bOverflowBoundsMatch = Overflow.ExportBounds.IsSet()
		&& FMath::IsNearlyEqual(Overflow.ExportBounds.GetValue().Min.X, -10.0)
		&& FMath::IsNearlyEqual(Overflow.ExportBounds.GetValue().Max.X, 77.0);
	if (!bOverflowBoundsMatch)
	{
		AddInfo(Overflow.ExportBounds.IsSet()
			? FString::Printf(TEXT("Nested script ref actual export bounds: min=(%.3f, %.3f), max=(%.3f, %.3f); expected X=[-10, 77]"),
				Overflow.ExportBounds.GetValue().Min.X, Overflow.ExportBounds.GetValue().Min.Y,
				Overflow.ExportBounds.GetValue().Max.X, Overflow.ExportBounds.GetValue().Max.Y)
			: FString(TEXT("Nested script ref export bounds are unset; expected X=[-10, 77]")));
	}
	TestTrue(TEXT("Expanded ref position and generated overflow reach bounds"), bOverflowBoundsMatch);
	TestEqual(TEXT("Bounds reuse the one actual script expansion"), OverflowPreparations, 1);
	TestTrue(TEXT("Script source remains unexpanded"), Document.Children[0]->Children.IsEmpty());

	auto FlowScript = MakeShared<FJsonObject>(); FlowScript->Values = ScriptNode->Values;
	FlowScript->SetStringField(TEXT("id"), TEXT("flow-script")); FlowScript->SetBoolField(TEXT("reusable"), false);
	FlowScript->SetStringField(TEXT("width"), TEXT("fill_container"));
	auto FlowFrame = MakeShared<FJsonObject>(); FlowFrame->SetStringField(TEXT("id"), TEXT("flow")); FlowFrame->SetStringField(TEXT("type"), TEXT("frame"));
	FlowFrame->SetStringField(TEXT("layout"), TEXT("horizontal")); FlowFrame->SetNumberField(TEXT("width"), 200); FlowFrame->SetNumberField(TEXT("height"), 50);
	FlowFrame->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(FlowScript)});
	DocumentJSON->SetArrayField(TEXT("children"), {MakeShared<FJsonValueObject>(FlowFrame)});
	FDesignDocument FlowDocument;
	if (!TestTrue(TEXT("Flow fixture parsed"), FPenParser::ParseDocumentObject(DocumentJSON, FlowDocument))) return false;
	const auto FlowResult = FDesign2UMGConverter::Convert(FlowDocument, FDesign2UMGConversionSettings::Defaults());
	TestTrue(*FlowResult.ConversionError, FlowResult.ConversionError.IsEmpty());
	Pending = {&FlowResult.RootWidget}; bool SawFlowWidth = false;
	while (!Pending.IsEmpty())
	{
#if UE_VERSION_OLDER_THAN(5, 4, 0)
		const auto* Widget = Pending.Pop(false);
#else
		const auto* Widget = Pending.Pop(EAllowShrinking::No);
#endif
		if (Widget->Name == TEXT("keep")) SawFlowWidth |= FMath::IsNearlyEqual(Widget->Bounds.W, 200.0f);
		for (const auto& Child : Widget->Children) Pending.Add(&Child);
	}
	TestTrue(TEXT("Flow fill width is resolved before evaluating script"), SawFlowWidth);
	TestTrue(TEXT("Generated overflow is included in export bounds"), FlowResult.ExportBounds.IsSet()
		&& FMath::IsNearlyEqual(FlowResult.ExportBounds.GetValue().Max.X, 207.0));
	FlowDocument.Children[0]->LayoutData.ExplicitClip = true;
	const auto Clipped = FDesign2UMGConverter::Convert(FlowDocument, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Parent clip keeps its existing export extent"), Clipped.ConversionError.IsEmpty()
		&& Clipped.ExportBounds.IsSet() && FMath::IsNearlyEqual(Clipped.ExportBounds.GetValue().Max.X, 200.0));
	FlowDocument.Children[0]->LayoutData.ExplicitClip = false;
	const FString FontSource = TEXT("/** @schema 2.11 */\nreturn [{type:'text',content:'Prepared text',fontFamily:'Roboto',fontSize:18,width:'fit_content',height:'fit_content'}];");
	FFileHelper::SaveStringToFile(FontSource, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	int32 PreparationCount = 0;
	const auto Prepared = FDesign2UMGConverter::Convert(FlowDocument, FDesign2UMGConversionSettings::Defaults(),
		[this, &PreparationCount](const FDesignDocument& Generated, TArray<FString>& Warnings, FString& Error,
			FDesign2UMGConverter::EFontPreparation Preparation)
		{
			++PreparationCount;
			if (!TestEqual(TEXT("Preparation sees only the actual generated subtree"), Generated.Children.Num(), 1)) return false;
			TestTrue(TEXT("Script preparation retains source import behavior"), Preparation == FDesign2UMGConverter::EFontPreparation::ImportSources);
			const auto& Script = Generated.Children.Last();
			TestEqual(TEXT("Flow width is resolved before script font preparation"), Script->Transform.Width.Value, 200.0f);
			if (!TestEqual(TEXT("Generated text is present"), Script->Children.Num(), 1)) return false;
			const auto& Text = Script->Children[0];
			TestTrue(TEXT("Text font exists before its first size estimate"),
				Text->Type == EDesignNodeType::Text && Text->Text.FontFamily == TEXT("Roboto")
				&& Text->Transform.Width.IsFit() && Text->Transform.Width.Value == 0.0f);
			Warnings.Add(TEXT("font preparation warning"));
			return true;
		});
	TestTrue(*Prepared.ConversionError, Prepared.ConversionError.IsEmpty());
	TestEqual(TEXT("One preparation for one actual script expansion"), PreparationCount, 1);
	TestTrue(TEXT("Font preparation warnings reach the conversion result"), Prepared.Warnings.Contains(TEXT("font preparation warning")));
	TestTrue(TEXT("Conversion does not attach generated children to the source document"), FlowDocument.Children[0]->Children[0]->Children.IsEmpty());
	const auto PreparationFailed = FDesign2UMGConverter::Convert(FlowDocument, FDesign2UMGConversionSettings::Defaults(),
		[](const FDesignDocument&, TArray<FString>&, FString& Error, FDesign2UMGConverter::EFontPreparation)
		{
			Error = TEXT("font preparation failure");
			return false;
		});
	TestEqual(TEXT("Font preparation errors keep their cause"), PreparationFailed.ConversionError, FString(TEXT("font preparation failure")));
	TestTrue(TEXT("Font preparation failure produces no partial widget tree"), PreparationFailed.RootWidget.Type.IsEmpty());
	FFileHelper::SaveStringToFile(TEXT("/** @schema 2.11 */\nthrow new Error('intentional');"), *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	const auto Failed = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestFalse(TEXT("Script error reaches production conversion result"), Failed.ConversionError.IsEmpty());
	TestTrue(TEXT("Failed script produces no partial asset tree"), Failed.RootWidget.Type.IsEmpty());
	return true;
#endif
}
#endif
