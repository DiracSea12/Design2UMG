IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTreeBuilderStructureSnapshotStableTest,
	"Design2UMG.TreeBuilder.StructureSnapshotStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTreeBuilderStructureSnapshotStableTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_SnapFixture");

	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("snap_root");
	Data.RootWidget.Name = TEXT("snap_root");
	Data.RootWidget.Bounds = {0.0f, 0.0f, 400.0f, 300.0f};

	FUMGWidgetNode BorderNode;
	BorderNode.Type = TEXT("Border");
	BorderNode.Id = TEXT("snap_border");
	BorderNode.Name = TEXT("snap_border");
	BorderNode.Bounds = {0.0f, 0.0f, 200.0f, 100.0f};

	FUMGWidgetNode TextNode;
	TextNode.Type = TEXT("TextBlock");
	TextNode.Id = TEXT("snap_text");
	TextNode.Name = TEXT("snap_text");
	TextNode.Bounds = {0.0f, 0.0f, 100.0f, 30.0f};
	TextNode.Text.Text = TEXT("snap");
	BorderNode.Children.Add(TextNode);

	FUMGWidgetNode GridNode;
	GridNode.Type = TEXT("GridPanel");
	GridNode.Id = TEXT("snap_grid");
	GridNode.Name = TEXT("snap_grid");
	GridNode.Bounds = {0.0f, 110.0f, 200.0f, 100.0f};

	FUMGWidgetNode Img0;
	Img0.Type = TEXT("Image");
	Img0.Id = TEXT("snap_img0");
	Img0.Name = TEXT("snap_img0");
	Img0.Bounds = {0.0f, 0.0f, 80.0f, 80.0f};

	FUMGWidgetNode Img1;
	Img1.Type = TEXT("Image");
	Img1.Id = TEXT("snap_img1");
	Img1.Name = TEXT("snap_img1");
	Img1.Bounds = {90.0f, 0.0f, 80.0f, 80.0f};

	GridNode.Children.Add(Img0);
	GridNode.Children.Add(Img1);

	Data.RootWidget.Children.Add(BorderNode);
	Data.RootWidget.Children.Add(GridNode);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/SnapFixture");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Snapshot fixture blueprint built"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Snapshot fixture generated class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists for snapshot test"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Snapshot fixture widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree || !WidgetInstance->WidgetTree->RootWidget)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const FString Actual = SerializeWidgetTree(WidgetInstance->WidgetTree->RootWidget).TrimEnd();

	const FString Expected = TEXT(
		"CanvasPanel:snap_root[none]\n"
		"  Border:snap_border[CanvasPanelSlot]\n"
		"    TextBlock:snap_text[BorderSlot]\n"
		"  GridPanel:snap_grid[CanvasPanelSlot]\n"
		"    Image:snap_img0[GridSlot]\n"
		"    Image:snap_img1[GridSlot]"
	);

	TestEqual(TEXT("Widget tree structure snapshot matches"), Actual, Expected);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNamedSlotOverflowWrapStructureStableTest,
	"Design2UMG.TreeBuilder.NamedSlotOverflowWrapStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNamedSlotOverflowWrapStructureStableTest::RunTest(const FString& Parameters)
{
	UWidgetBlueprint* Blueprint = NewObject<UWidgetBlueprint>(GetTransientPackage(), NAME_None, RF_Transient);
	TestNotNull(TEXT("Transient blueprint exists"), Blueprint);
	if (!Blueprint)
	{
		return false;
	}

	Blueprint->WidgetTree = NewObject<UWidgetTree>(Blueprint, UWidgetTree::StaticClass(), NAME_None, RF_Transient);
	TestNotNull(TEXT("Transient widget tree exists"), Blueprint->WidgetTree.Get());
	if (!Blueprint->WidgetTree)
	{
		return false;
	}

	FUMGWidgetNode RootNode;
	RootNode.Type = TEXT("CanvasPanel");
	RootNode.Id = TEXT("slot_root");
	RootNode.Name = TEXT("slot_root");
	RootNode.Bounds = {0.0f, 0.0f, 400.0f, 240.0f};

	FUMGWidgetNode HostNode;
	HostNode.Type = TEXT("ExpandableArea");
	HostNode.Id = TEXT("slot_host");
	HostNode.Name = TEXT("slot_host");
	HostNode.Bounds = {0.0f, 0.0f, 320.0f, 180.0f};

	FUMGWidgetNode HeaderNode;
	HeaderNode.Type = TEXT("TextBlock");
	HeaderNode.Id = TEXT("slot_header");
	HeaderNode.Name = TEXT("slot_header");
	HeaderNode.Bounds = {0.0f, 0.0f, 120.0f, 24.0f};
	HeaderNode.Text.Text = TEXT("Header");
	HeaderNode.WidgetStyle.SlotName = TEXT("Header");

	FUMGWidgetNode BodyPrimaryNode;
	BodyPrimaryNode.Type = TEXT("TextBlock");
	BodyPrimaryNode.Id = TEXT("slot_body_primary");
	BodyPrimaryNode.Name = TEXT("slot_body_primary");
	BodyPrimaryNode.Bounds = {0.0f, 28.0f, 180.0f, 24.0f};
	BodyPrimaryNode.Text.Text = TEXT("BodyPrimary");
	BodyPrimaryNode.WidgetStyle.SlotName = TEXT("Body");

	FUMGWidgetNode BodySecondaryNode;
	BodySecondaryNode.Type = TEXT("TextBlock");
	BodySecondaryNode.Id = TEXT("slot_body_secondary");
	BodySecondaryNode.Name = TEXT("slot_body_secondary");
	BodySecondaryNode.Bounds = {0.0f, 56.0f, 180.0f, 24.0f};
	BodySecondaryNode.Text.Text = TEXT("BodySecondary");
	BodySecondaryNode.WidgetStyle.SlotName = TEXT("Body");
	BodySecondaryNode.Slot.Padding = FMargin(3.0f, 0.0f, 0.0f, 7.0f);

	HostNode.Children.Add(HeaderNode);
	HostNode.Children.Add(BodyPrimaryNode);
	HostNode.Children.Add(BodySecondaryNode);
	RootNode.Children.Add(HostNode);

	UWidget* RootWidget = FUMGWidgetUtils::CreateWidgetRecursive(Blueprint->WidgetTree, RootNode);
	TestNotNull(TEXT("Root widget created"), RootWidget);
	if (!RootWidget)
	{
		return false;
	}
	Blueprint->WidgetTree->RootWidget = RootWidget;

	const UPanelWidget* RootPanel = Cast<UPanelWidget>(RootWidget);
	TestNotNull(TEXT("Root panel exists"), RootPanel);
	if (!RootPanel || RootPanel->GetChildrenCount() == 0)
	{
		return false;
	}

	UExpandableArea* HostWidget = Cast<UExpandableArea>(RootPanel->GetChildAt(0));
	TestNotNull(TEXT("ExpandableArea host created"), HostWidget);
	if (!HostWidget)
	{
		return false;
	}

	UWidget* HeaderContent = HostWidget->GetContentForSlot(TEXT("Header"));
	UWidget* BodyContent = HostWidget->GetContentForSlot(TEXT("Body"));
	TestNotNull(TEXT("Header slot received content"), HeaderContent);
	TestNotNull(TEXT("Body slot received content"), BodyContent);

	const UVerticalBox* BodyWrap = Cast<UVerticalBox>(BodyContent);
	TestNotNull(TEXT("Overflow body slot wrapped in VerticalBox"), BodyWrap);
	if (!HeaderContent || !BodyWrap)
	{
		return false;
	}

	TestEqual(TEXT("Header slot keeps explicit target"), HeaderContent->GetName(), FString(TEXT("slot_header")));
	TestEqual(TEXT("Wrap box keeps stable generated name"), BodyWrap->GetName(), FString(TEXT("slot_host_SlotWrap_Body")));
	TestEqual(TEXT("Body wrap child count"), BodyWrap->GetChildrenCount(), 2);
	TestEqual(TEXT("First wrapped child order"), BodyWrap->GetChildAt(0)->GetName(), FString(TEXT("slot_body_primary")));
	TestEqual(TEXT("Second wrapped child order"), BodyWrap->GetChildAt(1)->GetName(), FString(TEXT("slot_body_secondary")));

	const UPanelSlot* WrappedSecondSlot = BodyWrap->GetChildAt(1)->Slot;
	TestNotNull(TEXT("Wrapped second child has panel slot"), WrappedSecondSlot);
	if (const UVerticalBoxSlot* WrappedVBoxSlot = Cast<UVerticalBoxSlot>(WrappedSecondSlot))
	{
		const FMargin Padding = WrappedVBoxSlot->GetPadding();
		TestTrue(TEXT("Wrapped second child padding is preserved"), FMath::IsNearlyEqual(Padding.Left, 3.0f, 0.01f) && FMath::IsNearlyEqual(Padding.Bottom, 7.0f, 0.01f));
	}
	else
	{
		AddError(TEXT("Wrapped second child should use VerticalBoxSlot"));
		return false;
	}

	const FString Actual = SerializeWidgetTreeWithNamedSlots(RootWidget).TrimEnd();
	const FString Expected = TEXT(
		"CanvasPanel:slot_root[none]\n"
		"  ExpandableArea:slot_host[CanvasPanelSlot]\n"
		"    <NamedSlot:Header>\n"
		"      TextBlock:slot_header[none]\n"
		"    <NamedSlot:Body>\n"
		"      VerticalBox:slot_host_SlotWrap_Body[none]\n"
		"        TextBlock:slot_body_primary[VerticalBoxSlot]\n"
		"        TextBlock:slot_body_secondary[VerticalBoxSlot]");
	TestEqual(TEXT("Named-slot overflow structure snapshot matches"), Actual, Expected);

	return true;
}
