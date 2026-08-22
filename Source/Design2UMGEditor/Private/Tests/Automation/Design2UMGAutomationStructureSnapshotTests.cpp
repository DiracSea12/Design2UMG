// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Automation/Design2UMGAutomationStructureSnapshot.h"
#include "Design2UMGConverter.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "UMGJsonSerializer.h"
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

		return FString::Join(ChildFingerprints, TEXT("||"));
	}

	TSharedPtr<FJsonObject> ParseJsonObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		return FJsonSerializer::Deserialize(Reader, Object) ? Object : nullptr;
	}

	void StripGeneratedIdentityFieldsFromValue(const TSharedPtr<FJsonValue>& Value);

	void StripGeneratedIdentityFieldsFromObject(const TSharedPtr<FJsonObject>& Object)
	{
		if (!Object.IsValid())
		{
			return;
		}

		Object->RemoveField(TEXT("WidgetName"));
		Object->RemoveField(TEXT("Id"));
		Object->RemoveField(TEXT("Name"));
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
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

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Left->Values)
		{
			const TSharedPtr<FJsonValue>* RightValue = Right->Values.Find(Pair.Key);
			if (!RightValue || !JsonValuesEqual(Pair.Value, *RightValue))
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
		Document.Metadata.Add(TEXT("qa.case_name"), Variant.CaseName);
		Document.Metadata.Add(TEXT("qa.export_directory"), Variant.ExportDirectory);
		Document.Metadata.Add(TEXT("qa.historical_score"), Variant.HistoricalScore);
		Document.Metadata.Add(TEXT("qa.reference_identity"), Variant.ReferenceIdentity);
		Document.MutableSourceDocument().SourceFilePath = Variant.SourceFilePath;

		FDesignFont& SourceFont = Document.Fonts.AddDefaulted_GetRef();
		SourceFont.Name = Variant.SourceFontName;
		SourceFont.Url = Variant.SourceFilePath + TEXT("#font");

		TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
		Root->Id = Variant.RootId;
		Root->Name = Variant.RootName;
		Root->Type = EDesignNodeType::Frame;
		Root->Transform.SetWidthValue(FDesignSize::Fixed(320.0f));
		Root->Transform.SetHeightValue(FDesignSize::Fixed(160.0f));
		Root->LayoutData.Layout = EDesignLayout::Vertical;
		Root->LayoutData.SetGap(8.0f);
		Root->Metadata.Add(TEXT("qa.node_identity"), Variant.RootId);

		TSharedPtr<FDesignNode> Label = MakeShared<FDesignNode>();
		Label->Id = Variant.ChildId;
		Label->Name = Variant.ChildName;
		Label->Type = EDesignNodeType::Text;
		Label->Transform.SetWidthValue(FDesignSize::Fixed(320.0f));
		Label->Transform.SetHeightValue(FDesignSize::Fixed(32.0f));
		Label->Text.Content = TEXT("Identity invariant content");
		Label->Text.FontFamily = TEXT("Roboto");
		Label->Text.FontSize = 16.0f;

		TSharedPtr<FDesignNode> Icon = MakeShared<FDesignNode>();
		Icon->Id = Variant.ChildId + TEXT("_icon");
		Icon->Name = Variant.ChildName + TEXT("_Icon");
		Icon->Type = EDesignNodeType::IconFont;
		Icon->Transform.SetWidthValue(FDesignSize::Fixed(24.0f));
		Icon->Transform.SetHeightValue(FDesignSize::Fixed(24.0f));
		Icon->Text.IconFontFamily = Variant.SourceFontName;
		Icon->Text.IconFontName = TEXT("home");
		Icon->Text.IconWeight = 400;
		Icon->Text.FontSize = 16.0f;

		Root->Children.Add(Label);
		Root->Children.Add(Icon);
		Document.Children.Add(Root);
		return Document;
	}

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
		Component->bExpandedRefInstance || Screen->bExpandedRefInstance || Instance->bExpandedRefInstance);
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
		Component->bExpandedRefInstance || Screen->bExpandedRefInstance || Instance->bExpandedRefInstance);
	TestFalse(TEXT("Snapshot traversal must not inject expanded ref metadata into source nodes"),
		Component->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Screen->Metadata.Contains(TEXT("design2umg.expanded_ref_instance"))
		|| Instance->Metadata.Contains(TEXT("design2umg.expanded_ref_instance")));
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
			TEXT("qa005_baseline"),
			TEXT("root_baseline"),
			TEXT("Step2Frame"),
			TEXT("label_baseline"),
			TEXT("BodyLabel"),
			TEXT("Designs/pencil-welcome.pen"),
			TEXT("Automation/qa005/baseline"),
			TEXT("71/71"),
			TEXT("reference-baseline"),
			TEXT("lucide")
		},
		{
			TEXT("qa005_renamed_case"),
			TEXT("root_variant_a"),
			TEXT("RenamedStepFrame"),
			TEXT("label_variant_a"),
			TEXT("RenamedBodyLabel"),
			TEXT("Temp/qa005/renamed-source.pen"),
			TEXT("Automation/qa005/variant-a"),
			TEXT("70/71"),
			TEXT("reference-variant-a"),
			TEXT("Lucide Icons")
		},
		{
			TEXT("qa005_renamed_reference"),
			TEXT("root_variant_b"),
			TEXT("ReferenceRenamedFrame"),
			TEXT("label_variant_b"),
			TEXT("ReferenceBodyLabel"),
			TEXT("Fixtures/qa005/reference-renamed.pen"),
			TEXT("Automation/qa005/variant-b"),
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

#endif // WITH_AUTOMATION_WORKER
