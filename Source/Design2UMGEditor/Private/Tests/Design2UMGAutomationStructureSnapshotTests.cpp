// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Automation/Design2UMGAutomationStructureSnapshot.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"

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
			+ TEXT(",expanded=") + (Node->Source().bExpandedRefInstance ? TEXT("true") : TEXT("false"))
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

		return FString::Join(ChildFingerprints, TEXT("||"));
	}
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

	const FString BeforeFingerprint = BuildDocumentFingerprint(Document);
	TestFalse(TEXT("Before snapshot source document should not mark expanded ref instance"),
		Component->Source().bExpandedRefInstance || Screen->Source().bExpandedRefInstance || Instance->Source().bExpandedRefInstance);
	TestFalse(TEXT("Before snapshot source document should not contain expanded ref metadata"),
		Component->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Screen->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Instance->Metadata.Contains(TEXT("design2umg.expanded_ref_instance")));

	const TSharedPtr<FJsonObject> Snapshot = Design2UMGAutomationStructureSnapshot::BuildDesignStructureSnapshot(Document, TEXT("screen"));
	TestTrue(TEXT("Structure snapshot is produced"), Snapshot.IsValid());

	const FString AfterFingerprint = BuildDocumentFingerprint(Document);
	TestEqual(TEXT("Building structure snapshot must not mutate source document"), AfterFingerprint, BeforeFingerprint);
	TestTrue(TEXT("Original instance remains a ref node"), Instance->Type == EDesignNodeType::Ref && Instance->Ref == TEXT("shared_component"));
	TestFalse(TEXT("Snapshot traversal must not mark original nodes as expanded ref instances"),
		Component->Source().bExpandedRefInstance || Screen->Source().bExpandedRefInstance || Instance->Source().bExpandedRefInstance);
	TestFalse(TEXT("Snapshot traversal must not inject expanded ref metadata into source nodes"),
		Component->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Screen->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Instance->Metadata.Contains(TEXT("design2umg.expanded_ref_instance")));
	return true;
}

#endif // WITH_AUTOMATION_WORKER
