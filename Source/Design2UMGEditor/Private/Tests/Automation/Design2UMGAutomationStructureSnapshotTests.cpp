// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Automation/Design2UMGAutomationStructureSnapshot.h"
#include "Design2UMGConverter.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignTreeBudget.h"
#include "UMGJsonSerializer.h"
#include "../Design2UMGTestNodes.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/RetainerBox.h"
#include "Components/RichTextBlock.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"

#if WITH_AUTOMATION_WORKER
namespace
{
	FString BuildNodeFingerprint(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return TEXT("<null>");
		}

		TArray<FString> MetadataEntries;
		Node->Metadata.GetKeys(MetadataEntries);
		MetadataEntries.Sort();

		TArray<FString> DescendantKeys;
		Node->Descendants.GetKeys(DescendantKeys);
		DescendantKeys.Sort();

		TArray<FString> ChildFingerprints;
		ChildFingerprints.Reserve(Node->Children.Num());
		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			ChildFingerprints.Add(BuildNodeFingerprint(Child));
		}

		return TEXT("{id=") + Node->Id
			+ TEXT(",type=") + LexToString(static_cast<int32>(Node->Type))
			+ TEXT(",ref=") + Node->Ref
			+ TEXT(",expanded=") + (Node->bExpandedRefInstance ? TEXT("true") : TEXT("false"))
			+ TEXT(",meta=") + FString::Join(MetadataEntries, TEXT(","))
			+ TEXT(",desc=") + FString::Join(DescendantKeys, TEXT(","))
			+ TEXT(",children=") + FString::Join(ChildFingerprints, TEXT("|"))
			+ TEXT("}");
	}

	FString BuildDocumentFingerprint(const FDesignDocument& Doc)
	{
		TArray<FString> ChildFingerprints;
		ChildFingerprints.Reserve(Doc.Children.Num());
		for (const TSharedPtr<FDesignNode>& Child : Doc.Children)
		{
			ChildFingerprints.Add(BuildNodeFingerprint(Child));
		}
		for (const TSharedPtr<FDesignNode>& Imported : Doc.SourceDocument().ImportedReusableNodes)
		{
			ChildFingerprints.Add(BuildNodeFingerprint(Imported));
		}

		return FString::Join(ChildFingerprints, TEXT("||"));
	}

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	void StripGeneratedIdentityFieldsFromValue(const TSharedPtr<FJsonValue>& Value);

	void StripStructureNodeIdentityFields(const TSharedPtr<FJsonObject>& Snapshot)
	{
		if (!Snapshot.IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonValue>* StructureNodesValue = Snapshot->Values.Find(TEXT("structure_nodes"));
		if (!StructureNodesValue || !StructureNodesValue->IsValid() || (*StructureNodesValue)->Type != EJson::Array)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& StructureNodeValue : (*StructureNodesValue)->AsArray())
		{
			if (StructureNodeValue.IsValid() && StructureNodeValue->Type == EJson::Object)
			{
				StructureNodeValue->AsObject()->RemoveField(TEXT("id"));
				StructureNodeValue->AsObject()->RemoveField(TEXT("name"));
			}
		}
	}

	void StripGeneratedIdentityFieldsFromObject(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return;
		}

		Object->RemoveField(TEXT("WidgetName"));
		Object->RemoveField(TEXT("Id"));
		Object->RemoveField(TEXT("Name"));
		for (const auto& Pair : Object->Values)
		{
			StripGeneratedIdentityFieldsFromValue(Pair.Value);
		}
	}

	void StripGeneratedIdentityFieldsFromValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return;
		}

		if (Value->Type == EJson::Object)
		{
			StripGeneratedIdentityFieldsFromObject(Value->AsObject());
		}
		else if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Item : Value->AsArray())
			{
				StripGeneratedIdentityFieldsFromValue(Item);
			}
		}
	}

	bool JsonObjectsEqual(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right);

	bool JsonValuesEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid())
		{
			return !Left.IsValid() && !Right.IsValid();
		}
		if (Left->Type != Right->Type)
		{
			return false;
		}

		switch (Left->Type)
		{
		case EJson::Null:
		case EJson::None:
			return true;
		case EJson::Boolean:
			return Left->AsBool() == Right->AsBool();
		case EJson::Number:
			return FMath::IsNearlyEqual(Left->AsNumber(), Right->AsNumber());
		case EJson::String:
			return Left->AsString() == Right->AsString();
		case EJson::Object:
			return JsonObjectsEqual(Left->AsObject(), Right->AsObject());
		case EJson::Array:
			{
				const TArray<TSharedPtr<FJsonValue>>& LeftArray = Left->AsArray();
				const TArray<TSharedPtr<FJsonValue>>& RightArray = Right->AsArray();
				if (LeftArray.Num() != RightArray.Num())
				{
					return false;
				}

				for (int32 Index = 0; Index < LeftArray.Num(); ++Index)
				{
					if (!JsonValuesEqual(LeftArray[Index], RightArray[Index]))
					{
						return false;
					}
				}
				return true;
			}
		default:
			return false;
		}
	}

	bool JsonObjectsEqual(const TSharedPtr<FJsonObject>& Left, const TSharedPtr<FJsonObject>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid())
		{
			return !Left.IsValid() && !Right.IsValid();
		}
		if (Left->Values.Num() != Right->Values.Num())
		{
			return false;
		}

		for (const auto& Pair : Left->Values)
		{
			const TSharedPtr<FJsonValue> RightValue = Right->TryGetField(FString(*Pair.Key));
			if (!RightValue || !JsonValuesEqual(Pair.Value, RightValue))
			{
				return false;
			}
		}
		return true;
	}

	struct FIdentityVariant
	{
		FString CaseName;
		FString RootId;
		FString RootName;
		FString ChildId;
		FString ChildName;
		FString SourceFilePath;
		FString ExportDirectory;
		FString HistoricalScore;
		FString ReferenceIdentity;
		FString SourceFontName;
	};

	FDesignDocument BuildIdentityVariantDocument(const FIdentityVariant& Variant)
	{
		FDesignDocument Document;
		Document.Version = TEXT("1.0");
		Document.Metadata.Add(TEXT("test.case_name"), Variant.CaseName);
		Document.Metadata.Add(TEXT("test.export_directory"), Variant.ExportDirectory);
	Document.Metadata.Add(TEXT("test.historical_score"), Variant.HistoricalScore);
	Document.Metadata.Add(TEXT("test.reference_identity"), Variant.ReferenceIdentity);
		Document.MutableSourceDocument().SourceFilePath = Variant.SourceFilePath;

		FDesignFont& SourceFont = Document.Fonts.AddDefaulted_GetRef();
		SourceFont.Name = Variant.SourceFontName;
		SourceFont.Url = Variant.SourceFilePath + TEXT("#font");

		TSharedPtr<FDesignNode> Root = Design2UMGEditorTests::MakeNode(
			Variant.RootId, Variant.RootName, EDesignNodeType::Frame, 320.0f, 160.0f);
		Root->LayoutData.Layout = EDesignLayout::Vertical;
		Root->LayoutData.SetGap(8.0f);
	Root->Metadata.Add(TEXT("test.node_identity"), Variant.RootId);

		TSharedPtr<FDesignNode> Label = Design2UMGEditorTests::MakeNode(
			Variant.ChildId, Variant.ChildName, EDesignNodeType::Text, 320.0f, 32.0f);
		Label->Text.Content = TEXT("Identity invariant content");
		Label->Text.FontFamily = TEXT("Roboto");
		Label->Text.FontSize = 16.0f;

		TSharedPtr<FDesignNode> Icon = Design2UMGEditorTests::MakeNode(
			Variant.ChildId + TEXT("_icon"), Variant.ChildName + TEXT("_Icon"),
			EDesignNodeType::IconFont, 24.0f, 24.0f);
		Icon->Text.IconFontFamily = Variant.SourceFontName;
		Icon->Text.IconFontName = TEXT("home");
		Icon->Text.IconWeight = 400;
		Icon->Text.FontSize = 16.0f;

		Root->Children.Add(Label);
		Root->Children.Add(Icon);
		Document.Children.Add(Root);
		return Document;
	}

	// Deliberately local: unlike Design2UMGTestUtils::FindWidgetById (id match) this finds the
	// first widget carrying an icon payload (Text.IconName set), which has no shared equivalent.
	const FUMGWidgetNode* FindIconWidget(const FUMGWidgetNode& Node)
	{
		if (Node.Text.IconName.IsSet())
		{
			return &Node;
		}

		for (const FUMGWidgetNode& Child : Node.Children)
		{
			if (const FUMGWidgetNode* Match = FindIconWidget(Child))
			{
				return Match;
			}
		}
		return nullptr;
	}

	TSharedPtr<FDesignNode> MakeSnapshotReferenceNode(
		const FString& Id,
		const FString& TargetId,
		const bool bReusable)
	{
		TSharedPtr<FDesignNode> Node = MakeShared<FDesignNode>();
		Node->Id = Id;
		Node->Name = Id;
		Node->Type = EDesignNodeType::Ref;
		Node->Ref = TargetId;
		Node->bReusable = bReusable;
		return Node;
	}

	FDesignDocument BuildSnapshotNestedReferenceChain(const int32 ReferenceCount)
	{
		FDesignDocument Document;
		Document.Children.Reserve(ReferenceCount + 2);
		for (int32 Index = 0; Index <= ReferenceCount; ++Index)
		{
			const FString ComponentId = FString::Printf(TEXT("snapshot-component-%d"), Index);
			TSharedPtr<FDesignNode> Component = MakeShared<FDesignNode>();
			Component->Id = ComponentId;
			Component->Name = ComponentId;
			Component->Type = EDesignNodeType::Frame;
			Component->bReusable = true;
			if (Index < ReferenceCount)
			{
				Component->Children.Add(MakeSnapshotReferenceNode(
					FString::Printf(TEXT("snapshot-ref-%d"), Index),
					FString::Printf(TEXT("snapshot-component-%d"), Index + 1),
					false));
			}
			Document.Children.Add(MoveTemp(Component));
		}
		Document.Children.Add(MakeSnapshotReferenceNode(
			TEXT("snapshot-entry"),
			TEXT("snapshot-component-0"),
			false));
		return Document;
	}

	FDesignDocument BuildSnapshotNestedReferenceCycle()
	{
		FDesignDocument Document;
		TSharedPtr<FDesignNode> ComponentA = MakeShared<FDesignNode>();
		ComponentA->Id = TEXT("snapshot-cycle-a");
		ComponentA->Name = ComponentA->Id;
		ComponentA->Type = EDesignNodeType::Frame;
		ComponentA->bReusable = true;
		ComponentA->Children.Add(MakeSnapshotReferenceNode(
			TEXT("snapshot-cycle-a-to-b"),
			TEXT("snapshot-cycle-b"),
			false));

		TSharedPtr<FDesignNode> ComponentB = MakeShared<FDesignNode>();
		ComponentB->Id = TEXT("snapshot-cycle-b");
		ComponentB->Name = ComponentB->Id;
		ComponentB->Type = EDesignNodeType::Frame;
		ComponentB->bReusable = true;
		ComponentB->Children.Add(MakeSnapshotReferenceNode(
			TEXT("snapshot-cycle-b-to-a"),
			TEXT("snapshot-cycle-a"),
			false));

		Document.Children = {
			MoveTemp(ComponentA),
			MoveTemp(ComponentB),
			MakeSnapshotReferenceNode(TEXT("snapshot-cycle-entry"), TEXT("snapshot-cycle-a"), false)};
		return Document;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWidgetStructureSnapshotCarrierResponsibilityTest,
	"Design2UMG.Automation.WidgetStructureSnapshotCarrierResponsibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FWidgetStructureSnapshotCarrierResponsibilityTest::RunTest(const FString& Parameters)
{
	UWidgetTree* WidgetTree = NewObject<UWidgetTree>();
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("StrokePanel"));
	WidgetTree->RootWidget = Root;

	UBorder* Surface = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("StrokePanel_surface"));
	UCanvasPanel* Layout = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("StrokePanel_inner"));
	Surface->SetContent(Layout);
	Root->AddChild(Surface);

	UImage* Stroke = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("StrokePanel_stroke_top"));
	Root->AddChild(Stroke);

	URetainerBox* Mask = WidgetTree->ConstructWidget<URetainerBox>(URetainerBox::StaticClass(), TEXT("Masked_geometrymask"));
	USizeBox* SizedContent = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Masked_sz"));
	Mask->SetContent(SizedContent);
	Root->AddChild(Mask);

	const TSharedPtr<FJsonObject> Snapshot =
		Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(WidgetTree);
	TestTrue(TEXT("Widget structure snapshot is produced"), Snapshot.IsValid());
	if (!Snapshot.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ReasonHistogram = nullptr;
	TestTrue(TEXT("Snapshot exposes carrier responsibility histogram"),
		Snapshot->TryGetObjectField(TEXT("carrier_reason_histogram"), ReasonHistogram));
	if (!ReasonHistogram || !ReasonHistogram->IsValid())
	{
		return false;
	}

	TestEqual(TEXT("Explicit stroke overlay is classified"),
		static_cast<int32>((*ReasonHistogram)->GetNumberField(TEXT("Overlay.explicit_side_stroke"))), 1);
	TestEqual(TEXT("Explicit stroke surface is classified"),
		static_cast<int32>((*ReasonHistogram)->GetNumberField(TEXT("Border.explicit_stroke_surface"))), 1);
	TestEqual(TEXT("Absolute layout carrier is classified"),
		static_cast<int32>((*ReasonHistogram)->GetNumberField(TEXT("CanvasPanel.absolute_layout"))), 1);
	TestEqual(TEXT("Geometry-mask carrier is classified"),
		static_cast<int32>((*ReasonHistogram)->GetNumberField(TEXT("RetainerBox.geometry_mask"))), 1);
	TestEqual(TEXT("Size constraint carrier is classified"),
		static_cast<int32>((*ReasonHistogram)->GetNumberField(TEXT("SizeBox.size_constraint"))), 1);

	const TSharedPtr<FJsonObject>* SingleChildHistogram = nullptr;
	TestTrue(TEXT("Snapshot exposes single-child carrier patterns"),
		Snapshot->TryGetObjectField(TEXT("single_child_carrier_histogram"), SingleChildHistogram));
	if (!SingleChildHistogram || !SingleChildHistogram->IsValid())
	{
		return false;
	}
	TestEqual(TEXT("Stroke surface to layout chain is counted"),
		static_cast<int32>((*SingleChildHistogram)->GetNumberField(
			TEXT("Border.explicit_stroke_surface>CanvasPanel"))), 1);
	TestEqual(TEXT("Geometry mask to size constraint chain is counted"),
		static_cast<int32>((*SingleChildHistogram)->GetNumberField(
			TEXT("RetainerBox.geometry_mask>SizeBox"))), 1);

	const TArray<TSharedPtr<FJsonValue>>* StructureNodes = nullptr;
	TestTrue(TEXT("Snapshot exposes ordered structure nodes"),
		Snapshot->TryGetArrayField(TEXT("structure_nodes"), StructureNodes));
	if (!StructureNodes || StructureNodes->Num() < 4)
	{
		return false;
	}
	const TSharedPtr<FJsonObject> RootRecord = (*StructureNodes)[0]->AsObject();
	const TSharedPtr<FJsonObject> SurfaceRecord = (*StructureNodes)[1]->AsObject();
	TestTrue(TEXT("Root structure record is valid"), RootRecord.IsValid());
	TestTrue(TEXT("Surface structure record is valid"), SurfaceRecord.IsValid());
	if (!RootRecord.IsValid() || !SurfaceRecord.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("Root structure path is stable"), RootRecord->GetStringField(TEXT("path")), FString(TEXT("0")));
	TestEqual(TEXT("Root structure name is preserved"), RootRecord->GetStringField(TEXT("name")), FString(TEXT("StrokePanel")));
	TestEqual(TEXT("Surface structure path preserves parent and order"), SurfaceRecord->GetStringField(TEXT("path")), FString(TEXT("0/0")));
	TestEqual(TEXT("Surface structure name is preserved"), SurfaceRecord->GetStringField(TEXT("name")), FString(TEXT("StrokePanel_surface")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignStructureSnapshotDoesNotMutateSourceDocumentTest,
	"Design2UMG.Automation.StructureSnapshotDoesNotMutateSourceDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignStructureSnapshotDoesNotMutateSourceDocumentTest::RunTest(const FString& Parameters)
{
	FDesignDocument Document;

	TSharedPtr<FDesignNode> Component = MakeShared<FDesignNode>();
	Component->Id = TEXT("shared_component");
	Component->Name = TEXT("SharedComponent");
	Component->Type = EDesignNodeType::Frame;
	Component->bReusable = true;

	TSharedPtr<FDesignNode> Label = MakeShared<FDesignNode>();
	Label->Id = TEXT("shared_label");
	Label->Name = TEXT("SharedLabel");
	Label->Type = EDesignNodeType::Text;
	Label->Text.Content = TEXT("Snapshot Probe");
	Component->Children.Add(Label);

	TSharedPtr<FDesignNode> Screen = MakeShared<FDesignNode>();
	Screen->Id = TEXT("screen");
	Screen->Name = TEXT("Screen");
	Screen->Type = EDesignNodeType::Frame;

	TSharedPtr<FDesignNode> Instance = MakeShared<FDesignNode>();
	Instance->Id = TEXT("shared_component_instance");
	Instance->Name = TEXT("SharedComponentInstance");
	Instance->Type = EDesignNodeType::Ref;
	Instance->Ref = TEXT("shared_component");
	Screen->Children.Add(Instance);

	Document.Children.Add(Component);
	Document.Children.Add(Screen);
	TSharedPtr<FDesignNode> ImportedComponent = MakeShared<FDesignNode>();
	ImportedComponent->Id = TEXT("library:component");
	ImportedComponent->Type = EDesignNodeType::Frame;
	ImportedComponent->bReusable = true;
	TSharedPtr<FDesignNode> ImportedLabel = MakeShared<FDesignNode>();
	ImportedLabel->Id = TEXT("library:label");
	ImportedLabel->Name = TEXT("ImportedLabel");
	ImportedLabel->Type = EDesignNodeType::Text;
	ImportedLabel->Text.Content = TEXT("Imported Snapshot Probe");
	ImportedComponent->Children.Add(ImportedLabel);
	Document.MutableSourceDocument().ImportedReusableNodes.Add(ImportedComponent);
	TSharedPtr<FDesignNode> ImportedInstance = MakeShared<FDesignNode>();
	ImportedInstance->Id = TEXT("imported_instance");
	ImportedInstance->Name = TEXT("ImportedInstance");
	ImportedInstance->Type = EDesignNodeType::Ref;
	ImportedInstance->Ref = ImportedComponent->Id;
	Screen->Children.Add(ImportedInstance);

	const FString BeforeFingerprint = BuildDocumentFingerprint(Document);
	TestFalse(TEXT("Before snapshot source document should not mark expanded ref instance"),
		Component->bExpandedRefInstance || Screen->bExpandedRefInstance || Instance->bExpandedRefInstance);
	TestFalse(TEXT("Before snapshot source document should not contain expanded ref metadata"),
		Component->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Screen->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Instance->Metadata.Contains(TEXT("design2umg.expanded_ref_instance")));

	const TSharedPtr<FJsonObject> Snapshot = Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(Document, TEXT("screen"));
	if (!TestTrue(TEXT("Structure snapshot is produced"), Snapshot.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("Snapshot includes local and imported component children"), Snapshot->GetIntegerField(TEXT("total_nodes")), 5);
	TestEqual(TEXT("Snapshot counts both component labels"), Snapshot->GetIntegerField(TEXT("text_nodes")), 2);

	const FString AfterFingerprint = BuildDocumentFingerprint(Document);
	TestEqual(TEXT("Building structure snapshot must not mutate source document"), AfterFingerprint, BeforeFingerprint);
	TestTrue(TEXT("Original instance remains a ref node"), Instance->Type == EDesignNodeType::Ref && Instance->Ref == TEXT("shared_component"));
	TestFalse(TEXT("Snapshot traversal must not mark original nodes as expanded ref instances"),
		Component->bExpandedRefInstance || Screen->bExpandedRefInstance || Instance->bExpandedRefInstance);
	TestFalse(TEXT("Snapshot traversal must not inject expanded ref metadata into source nodes"),
		Component->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Screen->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Instance->Metadata.Contains(TEXT("design2umg.expanded_ref_instance")));
	TestFalse(TEXT("Snapshot traversal leaves imported definitions unexpanded"),
		ImportedComponent->bExpandedRefInstance || ImportedLabel->bExpandedRefInstance);
	TestEqual(TEXT("Snapshot preserves imported label content"), ImportedLabel->Text.Content, FString(TEXT("Imported Snapshot Probe")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDesignIdentityVariantConversionParityTest,
	"Design2UMG.Automation.IdentityVariantConversionParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FDesignIdentityVariantConversionParityTest::RunTest(const FString& Parameters)
{
	const FString CanonicalFontAssetPath = TEXT("/Game/Design2UMG/Fonts/lucide.lucide");
	const TArray<FIdentityVariant> Variants =
	{
		{
			TEXT("identity_baseline"),
			TEXT("root_baseline"),
			TEXT("Step2Frame"),
			TEXT("label_baseline"),
			TEXT("BodyLabel"),
			TEXT("Designs/pencil-welcome.pen"),
			TEXT("Automation/identity-parity/baseline"),
			TEXT("71/71"),
			TEXT("reference-baseline"),
			TEXT("lucide")
		},
		{
			TEXT("identity_source_variant"),
			TEXT("root_variant_a"),
			TEXT("RenamedStepFrame"),
			TEXT("label_variant_a"),
			TEXT("RenamedBodyLabel"),
			TEXT("Temp/identity-parity/renamed-source.pen"),
			TEXT("Automation/identity-parity/source-variant"),
			TEXT("70/71"),
			TEXT("reference-variant-a"),
			TEXT("Lucide Icons")
		},
		{
			TEXT("identity_reference_variant"),
			TEXT("root_variant_b"),
			TEXT("ReferenceRenamedFrame"),
			TEXT("label_variant_b"),
			TEXT("ReferenceBodyLabel"),
			TEXT("Fixtures/identity-parity/reference-renamed.pen"),
			TEXT("Automation/identity-parity/reference-variant"),
			TEXT("69/71"),
			TEXT("reference-variant-b"),
			TEXT("LUCIDE")
		}
	};

	FDesign2UMGConversionSettings Settings = FDesign2UMGConversionSettings::Defaults();
	for (const FIdentityVariant& Variant : Variants)
	{
		Settings.IconFontAssetPaths.Add(Variant.SourceFontName, CanonicalFontAssetPath);
	}

	TSharedPtr<FJsonObject> BaselineJson;
	TSharedPtr<FJsonObject> BaselineStructureSnapshot;
	for (int32 VariantIndex = 0; VariantIndex < Variants.Num(); ++VariantIndex)
	{
		const FIdentityVariant& Variant = Variants[VariantIndex];
		FDesignDocument Document = BuildIdentityVariantDocument(Variant);
		const FUMGWidgetData Converted = FDesign2UMGConverter::Convert(Document, Settings);

		const FUMGWidgetNode* IconWidget = FindIconWidget(Converted.RootWidget);
		TestTrue(
			FString::Printf(TEXT("Identity variant %s produces an icon widget"), *Variant.CaseName),
			IconWidget != nullptr);
		if (IconWidget)
		{
			TestEqual(
				FString::Printf(TEXT("Identity variant %s resolves its source font"), *Variant.CaseName),
				IconWidget->Text.FontFamily.Get(FString()),
				CanonicalFontAssetPath);
		}

		TestFalse(
			FString::Printf(TEXT("Identity variant %s emits no font mapping changes"), *Variant.CaseName),
			Converted.IconFontMappingChanges.HasChanges());
		TestEqual(
			FString::Printf(TEXT("Identity variant %s keeps warning output stable"), *Variant.CaseName),
			Converted.Warnings.Num(),
			0);

		TSharedPtr<FJsonObject> VariantJson = ParseJsonObject(FUMGJsonSerializer::Serialize(Converted));
		TestTrue(
			FString::Printf(TEXT("Identity variant %s produces serialized JSON"), *Variant.CaseName),
			VariantJson.IsValid());
		if (!VariantJson.IsValid())
		{
			return false;
		}
		StripGeneratedIdentityFieldsFromObject(VariantJson);

		const TSharedPtr<FJsonObject> VariantStructureSnapshot =
			Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(Document);
		TestTrue(
			FString::Printf(TEXT("Identity variant %s produces a structure snapshot"), *Variant.CaseName),
			VariantStructureSnapshot.IsValid());
		if (!VariantStructureSnapshot.IsValid())
		{
			return false;
		}
		// node_names lists source node names, which the variants intentionally
		// change; the parity contract compares structure semantics, not names.
		VariantStructureSnapshot->RemoveField(TEXT("node_names"));
		// Structure-node ids and names are source identities too; renamed variants
		// must compare their topology and semantics rather than those identities.
		StripStructureNodeIdentityFields(VariantStructureSnapshot);

		if (VariantIndex == 0)
		{
			BaselineJson = VariantJson;
			BaselineStructureSnapshot = VariantStructureSnapshot;
			continue;
		}

		TestTrue(
			FString::Printf(TEXT("Identity variant %s matches baseline JSON after identity removal"), *Variant.CaseName),
			JsonObjectsEqual(BaselineJson, VariantJson));
		TestTrue(
			FString::Printf(TEXT("Identity variant %s matches baseline structure snapshot"), *Variant.CaseName),
			JsonObjectsEqual(BaselineStructureSnapshot, VariantStructureSnapshot));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStructureSnapshotReferenceTerminationTest,
	"Design2UMG.Automation.StructureSnapshotReferenceTermination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FStructureSnapshotReferenceTerminationTest::RunTest(const FString&)
{
	FDesignDocument Document = BuildSnapshotNestedReferenceChain(DesignTreeLimits::MaxNodeDepth + 1);
	const TSharedPtr<FJsonObject> Snapshot =
		Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(Document, TEXT("snapshot-entry"));
	if (!TestTrue(TEXT("Nested reference snapshot exists"), Snapshot.IsValid()))
	{
		return false;
	}
	TestTrue(
		TEXT("Nested reference snapshot stays within the shared tree-depth limit"),
		Snapshot->GetIntegerField(TEXT("max_depth")) < DesignTreeLimits::MaxNodeDepth);

	FDesignDocument CycleDocument = BuildSnapshotNestedReferenceCycle();
	const TSharedPtr<FJsonObject> CycleSnapshot =
		Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(
			CycleDocument,
			TEXT("snapshot-cycle-entry"));
	if (!TestTrue(TEXT("Nested reference cycle snapshot terminates"), CycleSnapshot.IsValid()))
	{
		return false;
	}
	TestTrue(
		TEXT("Nested reference cycle snapshot is bounded by the shared tree-depth limit"),
		CycleSnapshot->GetIntegerField(TEXT("max_depth")) < DesignTreeLimits::MaxNodeDepth);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRichTextStructureSnapshotTest,
	"Design2UMG.Automation.RichTextStructureSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRichTextStructureSnapshotTest::RunTest(const FString&)
{
	UWidgetTree* Tree = NewObject<UWidgetTree>();
	URichTextBlock* Text = Tree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(), TEXT("StyledLabel"));
	Tree->RootWidget = Text;
	Text->SetText(FText::FromString(TEXT("<accent>A&amp;B</> &lt;C&gt;")));
	const TSharedPtr<FJsonObject> Snapshot = Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(Tree);
	if (!TestTrue(TEXT("Snapshot exists"), Snapshot.IsValid())) return false;
	TestEqual(TEXT("Native rich text counts as one text node"), Snapshot->GetIntegerField(TEXT("text_nodes")), 1);
	const TArray<TSharedPtr<FJsonValue>>& Visible = Snapshot->GetArrayField(TEXT("visible_texts"));
	if (!TestEqual(TEXT("Visible text is recorded"), Visible.Num(), 1)) return false;
	TestEqual(TEXT("Markup is excluded and entities decoded using Slate semantics"), Visible[0]->AsString(), FString(TEXT("A&B <C>")));
	const TArray<TSharedPtr<FJsonValue>>& Nodes = Snapshot->GetArrayField(TEXT("structure_nodes"));
	TestEqual(TEXT("Native type is classified without waiving structure checks"), Nodes[0]->AsObject()->GetStringField(TEXT("kind")), FString(TEXT("text")));
	TestEqual(TEXT("Rich-text structure node records the decoded plain text"), Nodes[0]->AsObject()->GetStringField(TEXT("text")), FString(TEXT("A&B <C>")));

	UWidgetTree* PlainTree = NewObject<UWidgetTree>();
	UTextBlock* PlainText = PlainTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PlainLabel"));
	PlainTree->RootWidget = PlainText;
	PlainText->SetText(FText::FromString(TEXT("  dashboard\nstatus  ")));
	const TSharedPtr<FJsonObject> PlainSnapshot =
		Design2UMGAutomationStructureSnapshot::BuildWidgetTreeStructureSnapshot(PlainTree);
	if (!TestTrue(TEXT("Plain-text snapshot exists"), PlainSnapshot.IsValid())) return false;
	const TArray<TSharedPtr<FJsonValue>>& PlainVisible = PlainSnapshot->GetArrayField(TEXT("visible_texts"));
	if (!TestEqual(TEXT("Plain visible text remains recorded"), PlainVisible.Num(), 1)) return false;
	TestEqual(TEXT("Plain visible text is normalized"), PlainVisible[0]->AsString(), FString(TEXT("dashboard status")));
	const TArray<TSharedPtr<FJsonValue>>& PlainNodes = PlainSnapshot->GetArrayField(TEXT("structure_nodes"));
	TestEqual(TEXT("Plain structure node records the same normalized raw text"),
		PlainNodes[0]->AsObject()->GetStringField(TEXT("text")), FString(TEXT("dashboard status")));
	return true;
}

#endif // WITH_AUTOMATION_WORKER
