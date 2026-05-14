IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetadataCollectorExactSnapshotTest,
	"Design2UMG.WidgetBlueprint.MetadataCollector.ExactSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMetadataCollectorExactSnapshotTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Id = TEXT("inst1");
	Node.Name = TEXT("NavItem");
	Node.Type = TEXT("INSTANCE");
	Node.MutableSource().SourceNodeType = TEXT("INSTANCE");
	Node.MutableSource().ComponentId = TEXT("12:34");
	Node.MutableSource().ComponentSetId = TEXT("56:78");
	Node.MutableSource().VariantPropertiesJson = TEXT("{\"Active\":\"True\"}");

	UTextBlock* Widget = NewObject<UTextBlock>(GetTransientPackage(), NAME_None, RF_Transient);
	Widget->Rename(TEXT("NavItem_0"));

	TArray<FDesign2UMGFigmaWidgetMetadataRecord> Records;
	FWidgetMetadataCollector::TryAppend(Widget, Node, &Records);

	TestEqual(TEXT("Exactly one record appended"), Records.Num(), 1);
	if (Records.Num() != 1) return false;

	const FDesign2UMGFigmaWidgetMetadataRecord& R = Records[0];

	auto NormalizeJson = [](const FString& Raw) -> FString
	{
		TSharedPtr<FJsonObject> Obj;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			return Raw;
		}
		TArray<FString> Keys;
		for (const auto& Pair : Obj->Values) { Keys.Add(Pair.Key); }
		Keys.Sort();
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		Writer->WriteObjectStart();
		for (const FString& Key : Keys)
		{
			Writer->WriteValue(Key, Obj->GetStringField(Key));
		}
		Writer->WriteObjectEnd();
		Writer->Close();
		return Out;
	};

	const FString ActualSnapshot = FString::Printf(
		TEXT("WidgetId=%s|WidgetName=%s|SourceNodeType=%s|ComponentId=%s|ComponentSetId=%s|VariantProperties=%s"),
		*R.WidgetId,
		*R.WidgetName.ToString(),
		*R.SourceNode.SourceNodeType,
		*R.SourceNode.ComponentId,
		*R.SourceNode.ComponentSetId,
		*NormalizeJson(R.SourceNode.VariantPropertiesJson));

	const FString ExpectedSnapshot = FString::Printf(
		TEXT("WidgetId=%s|WidgetName=%s|SourceNodeType=%s|ComponentId=%s|ComponentSetId=%s|VariantProperties=%s"),
		TEXT("inst1"),
		*Widget->GetFName().ToString(),
		TEXT("INSTANCE"),
		TEXT("12:34"),
		TEXT("56:78"),
		*NormalizeJson(TEXT("{\"Active\":\"True\"}")));

	TestEqual(TEXT("Exact metadata snapshot matches"), ActualSnapshot, ExpectedSnapshot);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMetadataCollectorNoRecordWithoutSemanticsTest,
	"Design2UMG.WidgetBlueprint.MetadataCollector.NoRecordWithoutSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMetadataCollectorNoRecordWithoutSemanticsTest::RunTest(const FString& Parameters)
{
	FUMGWidgetNode Node;
	Node.Id = TEXT("frame1");
	Node.Name = TEXT("Frame");
	Node.Type = TEXT("FRAME");

	UTextBlock* Widget = NewObject<UTextBlock>(GetTransientPackage(), NAME_None, RF_Transient);
	Widget->Rename(TEXT("Frame_0"));

	TArray<FDesign2UMGFigmaWidgetMetadataRecord> Records;
	FWidgetMetadataCollector::TryAppend(Widget, Node, &Records);

	TestEqual(TEXT("No record appended for non-instance node"), Records.Num(), 0);
	return true;
}
