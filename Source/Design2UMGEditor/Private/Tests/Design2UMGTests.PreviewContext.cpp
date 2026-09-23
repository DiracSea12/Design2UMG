// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGEditorImportServices.h"
#include "DesignImportPlanning.h"
#include "DesignImportPipeline.h"
#include "UMGJsonSerializer.h"
#include "UMGSchemaSourceExtensions.h"
#include "UMGGenerator.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Misc/ScopeExit.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Components/WidgetSwitcher.h"
#include "Editor.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "WidgetBlueprint.h"

#if WITH_AUTOMATION_WORKER
namespace
{
	FUMGWidgetNode PreviewNode(const TCHAR* Id, const TCHAR* Type)
	{
		FUMGWidgetNode Node;
		Node.Id = Id;
		Node.Name = Id;
		Node.Type = Type;
		Node.Bounds = { 0, 0, 320, 180 };
		Node.MutableSource().SourceNodeId = Id;
		Node.MutableSource().SourceNodeType = Type;
		return Node;
	}

	FUMGWidgetData PreviewContextFixture()
	{
		FUMGWidgetData Data;
		Data.RootWidget = PreviewNode(TEXT("switcher"), TEXT("WidgetSwitcher"));
		Data.RootWidget.WidgetState.ActiveWidgetIndex = 0;
		FUMGWidgetNode Page = PreviewNode(TEXT("page_a"), TEXT("CanvasPanel"));
		FUMGWidgetNode Trigger = PreviewNode(TEXT("trigger"), TEXT("Button"));
		FUMGWidgetNode Label = PreviewNode(TEXT("label"), TEXT("TextBlock"));
		Label.Text.Text = TEXT("Original text");
		Trigger.Children.Add(Label);
		Page.Children.Add(Trigger);
		Data.RootWidget.Children.Add(Page);
		Data.RootWidget.Children.Add(PreviewNode(TEXT("page_b"), TEXT("CanvasPanel")));
		Data.MutableSourceDocument().Provider = TEXT("figma");
		Data.MutableSourceDocument().DocumentScope = TEXT("preview-context-file");
		Data.MutableSourceDocument().ComponentsJson = TEXT("{\"component\":{\"key\":\"button-key\"}}");
		Data.MutableSourceDocument().ComponentDependenciesJson = TEXT("{\"dependency\":{\"id\":\"dependency\"}}");
		Data.Warnings.Add(TEXT("Original conversion warning"));
		auto& Navigation = Data.MutablePrototypeNavigation();
		Navigation.SwitcherWidgetId = TEXT("switcher");
		Navigation.SwitcherWidgetName = TEXT("switcher");
		Navigation.PageIds = { TEXT("page_a"), TEXT("page_b") };
		auto& Binding = Navigation.ClickBindings.AddDefaulted_GetRef();
		Binding.TriggerWidgetId = TEXT("trigger");
		Binding.TriggerWidgetName = TEXT("trigger");
		Binding.SourcePageId = TEXT("page_a");
		Binding.TargetPageId = TEXT("page_b");
		Binding.TargetPageIndex = 1;
		auto& Step = Binding.Steps.AddDefaulted_GetRef();
		Step.TargetPageId = TEXT("page_b");
		Step.TargetPageIndex = 1;
		return Data;
	}

	FPreviewRefreshResult RefreshContext(const FUMGWidgetData& Original, const FUMGWidgetData& Edited)
	{
		FPreviewRefreshInput Input;
		Input.OriginalData = MakeShared<FUMGWidgetData>(Original);
		Input.EditedJson = FUMGJsonSerializer::Serialize(Edited);
		Input.bResolveImages = false;
		return DesignImportPipeline::AnalyzePreviewRefresh(Input);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewContextPreservationTest,
	"Design2UMG.PreviewContext.Preservation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewContextPreservationTest::RunTest(const FString& Parameters)
{
	const FUMGWidgetData Original = PreviewContextFixture();
	for (const bool bEdit : { false, true })
	{
		FUMGWidgetData Edited = Original;
		if (bEdit) Edited.RootWidget.Children[0].Children[0].Children[0].Text.Text = TEXT("Edited text");
		const auto Result = RefreshContext(Original, Edited);
		TestTrue(TEXT("Refresh succeeds"), Result.bSucceeded);
		if (!Result.bSucceeded) continue;
		TestEqual(TEXT("Provider preserved"), Result.Data.SourceDocument().Provider, Original.SourceDocument().Provider);
		TestEqual(TEXT("Scope preserved"), Result.Data.SourceDocument().DocumentScope, Original.SourceDocument().DocumentScope);
		TestEqual(TEXT("Component catalog preserved"), Result.Data.SourceDocument().ComponentsJson, Original.SourceDocument().ComponentsJson);
		TestEqual(TEXT("Dependencies preserved"), Result.Data.SourceDocument().ComponentDependenciesJson, Original.SourceDocument().ComponentDependenciesJson);
		TestTrue(TEXT("Conversion warnings preserved"), Result.Data.Warnings == Original.Warnings);
		const auto& Label = Result.Data.RootWidget.Children[0].Children[0].Children[0];
		TestEqual(TEXT("Stable source identity preserved"), Label.Source().SourceNodeId, FString(TEXT("label")));
		TestEqual(TEXT("Visual edit is not overwritten"), Label.Text.Text.Get(FString()), bEdit ? FString(TEXT("Edited text")) : FString(TEXT("Original text")));
		TestEqual(TEXT("Interaction retained"), Result.Data.PrototypeNavigation().ClickBindings.Num(), 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewContextStructureTest,
	"Design2UMG.PreviewContext.Structure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewContextStructureTest::RunTest(const FString& Parameters)
{
	const FUMGWidgetData Original = PreviewContextFixture();
	FUMGWidgetData Edited = Original;
	Edited.RootWidget.Children.Swap(0, 1);
	FUMGWidgetNode Added;
	Added.Id = TEXT("added"); Added.Name = TEXT("added"); Added.Type = TEXT("TextBlock");
	Added.Text.Text = TEXT("New text");
	Edited.RootWidget.Children[1].Children.Add(Added);
	const auto Result = RefreshContext(Original, Edited);
	TestTrue(TEXT("Reorder and a new ordinary node are accepted"), Result.bSucceeded);
	if (Result.bSucceeded)
	{
		TestFalse(TEXT("New node has no fabricated source"), Result.Data.RootWidget.Children[1].Children[1].HasSourceData());
		const auto& Bindings = Result.Data.PrototypeNavigation().ClickBindings;
		if (TestEqual(TEXT("Binding retained"), Bindings.Num(), 1))
		{
			TestEqual(TEXT("Binding page index recomputed"), Bindings[0].TargetPageIndex, 0);
			TestEqual(TEXT("Step page index recomputed"), Bindings[0].Steps[0].TargetPageIndex, 0);
		}
	}

	FUMGWidgetData OriginalWithOrdinary = Original;
	OriginalWithOrdinary.RootWidget.Children[0].Children.Add(Added);
	Edited = OriginalWithOrdinary;
	Edited.RootWidget.Children[0].Children.Pop();
	TestTrue(
		TEXT("Deleting an unreferenced source-less node remains allowed"),
		RefreshContext(OriginalWithOrdinary, Edited).bSucceeded);

	auto TestRejected = [this, &Original](FUMGWidgetData Candidate, const TCHAR* What)
	{
		const FPreviewRefreshResult Rejected = RefreshContext(Original, Candidate);
		TestFalse(What, Rejected.bSucceeded);
		TestFalse(TEXT("Rejected edit has a reason"), Rejected.ParseError.IsEmpty());
	};

	Edited = Original;
	Edited.RootWidget.Children[0].Children[0].Children[0].Id = TEXT("label_replaced");
	TestRejected(Edited, TEXT("Changing a source node Id is rejected"));

	Edited = Original;
	Edited.RootWidget.Children[0].Children[0].Children[0].Id.Reset();
	TestRejected(Edited, TEXT("Removing a retained source node Id is rejected"));

	Edited = Original;
	Edited.RootWidget.Children[0].Children[0].Children.Reset();
	TestTrue(
		TEXT("Deleting an unreferenced source node is accepted"),
		RefreshContext(Original, Edited).bSucceeded);

	for (const TCHAR* Type : { TEXT("Button"), TEXT("Image") })
	{
		FUMGWidgetData OriginalWithDeletableSource = Original;
		OriginalWithDeletableSource.RootWidget.Children[0].Children.Add(PreviewNode(Type, Type));
		Edited = OriginalWithDeletableSource;
		Edited.RootWidget.Children[0].Children.Pop();
		TestTrue(
			TEXT("Deleting an unreferenced source button or image is accepted"),
			RefreshContext(OriginalWithDeletableSource, Edited).bSucceeded);
	}

	Edited = Original;
	Edited.RootWidget.Children[0].Children[0].Children.Reset();
	Edited.RootWidget.Children[0].Children[0].Children.Add(Added);
	TestRejected(Edited, TEXT("Deleting a source node while adding an ordinary node is ambiguous"));

	FUMGWidgetData OriginalWithAnonymous = Original;
	FUMGWidgetNode ExistingAnonymous = Added;
	ExistingAnonymous.Id.Reset();
	OriginalWithAnonymous.RootWidget.Children[0].Children.Add(ExistingAnonymous);
	Edited = OriginalWithAnonymous;
	Edited.RootWidget.Children[0].Children.Pop();
	Edited.RootWidget.Children[0].Children[0].Children[0].Id.Reset();
	const FPreviewRefreshResult AnonymousReplacement = RefreshContext(OriginalWithAnonymous, Edited);
	TestFalse(
		TEXT("Replacing an existing anonymous node while clearing a source Id stays ambiguous"),
		AnonymousReplacement.bSucceeded);
	TestTrue(
		TEXT("Anonymous replacement explains the stable Id ambiguity"),
		AnonymousReplacement.ParseError.Contains(TEXT("ambiguous")));

	Edited = Original;
	Edited.RootWidget.Children[0].Children.Reset();
	TestRejected(Edited, TEXT("Deleting an interaction trigger is rejected"));

	Edited = Original;
	Edited.RootWidget.Children.RemoveAt(1);
	TestRejected(Edited, TEXT("Deleting an interaction target page is rejected"));

	Edited = Original;
	FUMGWidgetNode Duplicate = Edited.RootWidget.Children[0];
	Edited.RootWidget.Children.Add(MoveTemp(Duplicate));
	TestRejected(Edited, TEXT("Duplicate stable Id is rejected"));

	Edited = Original;
	Edited.RootWidget.Children[0].Children[0].Children[0].Type = TEXT("Image");
	TestRejected(Edited, TEXT("Changing a source node type is rejected"));

	Edited = Original;
	Edited.RootWidget.Children[0].Children[0].Name = TEXT("renamed_trigger");
	TestRejected(Edited, TEXT("Breaking a runtime button address is rejected"));

	Edited = Original;
	Edited.RootWidget.Children[0].Children[0].Name = TEXT("trigger_1");
	TestTrue(
		TEXT("A generated-name suffix keeps the unique runtime button address"),
		RefreshContext(Original, Edited).bSucceeded);

	TestTrue(TEXT("A corrected retry succeeds"), RefreshContext(Original, Original).bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewContextRuntimeAddressRolesTest,
	"Design2UMG.PreviewContext.RuntimeAddressRoles", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewContextRuntimeAddressRolesTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData Original = PreviewContextFixture();
	Original.MutablePrototypeNavigation().ClickBindings[0].Transition.Type = TEXT("SMART_ANIMATE");
	Original.MutablePrototypeNavigation().ClickBindings[0].Transition.DurationSeconds = 0.2f;

	FUMGWidgetData Edited = Original;
	FUMGWidgetNode CrossTypeName;
	CrossTypeName.Id = TEXT("cross_type_name");
	CrossTypeName.Name = TEXT("trigger_1");
	CrossTypeName.Type = TEXT("TextBlock");
	Edited.RootWidget.Children[0].Children.Add(CrossTypeName);
	TestTrue(
		TEXT("A TextBlock in the Button suffix family does not make the click trigger ambiguous"),
		RefreshContext(Original, Edited).bSucceeded);
	Edited.RootWidget.Children[0].Children.Last().Id.Reset();
	TestTrue(
		TEXT("A new source-less non-page node does not need a fabricated stable Id"),
		RefreshContext(Original, Edited).bSucceeded);

	Edited = Original;
	FUMGWidgetNode SecondButton = CrossTypeName;
	SecondButton.Id = TEXT("second_button");
	SecondButton.Type = TEXT("Button");
	Edited.RootWidget.Children[0].Children.Add(SecondButton);
	TestFalse(
		TEXT("A second Button in the trigger suffix family remains a real ambiguity"),
		RefreshContext(Original, Edited).bSucceeded);

	Edited = Original;
	FUMGWidgetNode PageAddressConflict = CrossTypeName;
	PageAddressConflict.Id = TEXT("ordinary_page_child");
	PageAddressConflict.Name = TEXT("page_b_1");
	Edited.RootWidget.Children.Add(PageAddressConflict);
	TestFalse(
		TEXT("A page-host child in the target suffix family remains a real ambiguity"),
		RefreshContext(Original, Edited).bSucceeded);

	Edited = Original;
	Edited.RootWidget.Children[0].Name = TEXT("page_b");
	Edited.RootWidget.Children[1].Name = TEXT("page_a");
	TestFalse(
		TEXT("A target page cannot borrow its runtime address from another stable page"),
		RefreshContext(Original, Edited).bSucceeded);

	FUMGWidgetData OriginalWithUnreferencedPage = Original;
	OriginalWithUnreferencedPage.RootWidget.Children.Add(
		PreviewNode(TEXT("unreferenced_page"), TEXT("CanvasPanel")));
	Edited = OriginalWithUnreferencedPage;
	Edited.RootWidget.Children.Last().Name = TEXT("renamed_unreferenced_page");
	TestTrue(
		TEXT("An unreferenced page remains freely renameable"),
		RefreshContext(OriginalWithUnreferencedPage, Edited).bSucceeded);

	Edited = Original;
	FUMGWidgetNode CrossTypeSwitcher = CrossTypeName;
	CrossTypeSwitcher.Id = TEXT("cross_type_switcher");
	CrossTypeSwitcher.Name = TEXT("switcher_1");
	Edited.RootWidget.Children[0].Children.Add(CrossTypeSwitcher);
	TestTrue(
		TEXT("A non-switcher in the switcher suffix family does not break animation or page lookup"),
		RefreshContext(Original, Edited).bSucceeded);

	Edited = Original;
	FUMGWidgetNode SecondSwitcher = CrossTypeSwitcher;
	SecondSwitcher.Id = TEXT("second_switcher");
	SecondSwitcher.Type = TEXT("WidgetSwitcher");
	Edited.RootWidget.Children[0].Children.Add(SecondSwitcher);
	TestFalse(
		TEXT("A second WidgetSwitcher in the switcher suffix family is rejected"),
		RefreshContext(Original, Edited).bSucceeded);

	FUMGWidgetData KeyboardOriginal = Original;
	KeyboardOriginal.MutablePrototypeNavigation().ClickBindings[0].TriggerType = TEXT("ON_KEY_DOWN");
	KeyboardOriginal.MutablePrototypeNavigation().ClickBindings[0].TriggerKeyCodes = { TEXT("ENTER") };
	Edited = KeyboardOriginal;
	Edited.RootWidget.Children[0].Children.Add(SecondButton);
	TestTrue(
		TEXT("Keyboard bindings use stable page ownership rather than Button-name lookup"),
		RefreshContext(KeyboardOriginal, Edited).bSucceeded);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewContextNestedPageOwnershipTest,
	"Design2UMG.PreviewContext.NestedPageOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewContextNestedPageOwnershipTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData Original = PreviewContextFixture();
	Original.MutableSourceDocument() = FUMGSourceDocumentData();
	Original.MutablePrototypeNavigation().ClickBindings[0].TriggerType = TEXT("ON_KEY_DOWN");
	Original.MutablePrototypeNavigation().ClickBindings[0].TriggerKeyCodes = { TEXT("ENTER") };
	Original.RootWidget.Children.Add(PreviewNode(TEXT("page_c"), TEXT("CanvasPanel")));
	Original.MutablePrototypeNavigation().PageIds.Add(TEXT("page_c"));
	FUMGWidgetData Edited = Original;
	FUMGWidgetNode NestedPage = MoveTemp(Edited.RootWidget.Children[0]);
	Edited.RootWidget.Children.RemoveAt(0);
	Edited.RootWidget.Children[1].Children.Add(MoveTemp(NestedPage));
	const auto Result = RefreshContext(Original, Edited);
	if (TestTrue(TEXT("Moving a page into another page is supported"), Result.bSucceeded))
	{
		const auto& Navigation = Result.Data.PrototypeNavigation();
		TestEqual(TEXT("Only actual hosted pages remain in navigation"), Navigation.PageIds.Num(), 2);
		TestEqual(TEXT("Trigger belongs to its current host page, not its former page"),
			Navigation.ClickBindings[0].SourcePageId, FString(TEXT("page_c")));
		TestEqual(TEXT("Target page index follows current host order"), Navigation.ClickBindings[0].TargetPageIndex, 0);

		const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/NestedPreviewPage_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };
		FUMGPrototypeNavigationData GeneratedNavigation;
		UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Result.Data, Folder, nullptr, nullptr, &GeneratedNavigation);
		if (!TestNotNull(TEXT("Edited prototype builds"), Blueprint)) return false;
		FUMGGenerator::SaveWidgetBlueprint(Blueprint, nullptr, &GeneratedNavigation);
		auto* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
		UUserWidget* Instance = CreateWidget<UUserWidget>(GEditor->GetEditorWorldContext().World(), GeneratedClass);
		if (!TestNotNull(TEXT("Edited prototype instantiates"), Instance)) return false;
		Instance->TakeWidget();
		ON_SCOPE_EXIT { Instance->RemoveFromParent(); };
		auto* Switcher = Cast<UWidgetSwitcher>(Instance->WidgetTree->FindWidget(FName(*Navigation.SwitcherWidgetName)));
		UFunction* KeyDown = GeneratedClass->FindFunctionByName(TEXT("OnKeyDown"));
		if (!TestNotNull(TEXT("Current page host exists"), Switcher) || !TestNotNull(TEXT("Keyboard binding is generated"), KeyDown)) return false;
		struct FKeyDownParameters { FGeometry Geometry; FKeyEvent KeyEvent; FEventReply Reply; } KeyParameters;
		KeyParameters.KeyEvent = FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0);
		Switcher->SetActiveWidgetIndex(0);
		Instance->ProcessEvent(KeyDown, &KeyParameters);
		TestEqual(TEXT("Key leaves an unrelated page unchanged"), Switcher->GetActiveWidgetIndex(), 0);
		Switcher->SetActiveWidgetIndex(1);
		Instance->ProcessEvent(KeyDown, &KeyParameters);
		TestEqual(TEXT("Key works on the current containing page"), Switcher->GetActiveWidgetIndex(), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewContextOverlayTest,
    "Design2UMG.PreviewContext.OverlayReferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewContextOverlayTest::RunTest(const FString& Parameters)
{
    FUMGWidgetData Original = PreviewContextFixture();
    FUMGWidgetNode Switcher = MoveTemp(Original.RootWidget);
    Original.RootWidget = PreviewNode(TEXT("root"), TEXT("Overlay"));
    Original.RootWidget.Children.Add(MoveTemp(Switcher));
    FUMGWidgetNode Host = PreviewNode(TEXT("overlay_host"), TEXT("Overlay"));
    Host.Children.Add(PreviewNode(TEXT("overlay_page"), TEXT("CanvasPanel")));
    Original.RootWidget.Children.Add(MoveTemp(Host));
    auto& Navigation = Original.MutablePrototypeNavigation();
    Navigation.OverlayHostWidgetId = TEXT("overlay_host");
    Navigation.OverlayHostWidgetName = TEXT("overlay_host");
    Navigation.PageIds.Add(TEXT("overlay_page"));
    auto& Binding = Navigation.ClickBindings[0];
    Binding.Action = EUMGPrototypeNavigationAction::Overlay;
    Binding.TargetPageId = TEXT("overlay_page");
    Binding.TargetPageIndex = 2;
    Binding.Steps[0].Kind = EUMGPrototypeNavigationStepKind::Overlay;
    Binding.Steps[0].TargetPageId = TEXT("overlay_page");
    Binding.Steps[0].TargetPageIndex = 2;
    FUMGWidgetData Edited = Original;
    Edited.RootWidget.Render.RenderOpacity = 0.8f;
    TestTrue(TEXT("Overlay-only target survives a visual edit"), RefreshContext(Original, Edited).bSucceeded);
    Edited.RootWidget.Children[1].Children.Reset();
    TestFalse(TEXT("Deleting overlay target rejects confirmation"), RefreshContext(Original, Edited).bSucceeded);
    Edited = Original;
    FUMGWidgetNode Trigger = MoveTemp(Edited.RootWidget.Children[0].Children[0].Children[0]);
    Edited.RootWidget.Children[0].Children[0].Children.Reset();
    Edited.RootWidget.Children[0].Children[1].Children.Add(MoveTemp(Trigger));
    const auto Moved = RefreshContext(Original, Edited);
    if (TestTrue(TEXT("Moving trigger between pages is supported"), Moved.bSucceeded))
	TestEqual(TEXT("Trigger page is reconciled"), Moved.Data.PrototypeNavigation().ClickBindings[0].SourcePageId, FString(TEXT("page_b")));

	FUMGWidgetData MirrorOriginal = PreviewContextFixture();
	FUMGWidgetNode MirrorSwitcher = MoveTemp(MirrorOriginal.RootWidget);
	MirrorOriginal.RootWidget = PreviewNode(TEXT("root"), TEXT("Overlay"));
	MirrorOriginal.RootWidget.Children.Add(MoveTemp(MirrorSwitcher));
	FUMGWidgetNode MirrorHost = PreviewNode(TEXT("overlay_host"), TEXT("Overlay"));
	const FString MirrorId = FString(TEXT("page_b")) + Design2UMGPrototypeTokens::OverlayMirrorSuffix();
	FUMGWidgetNode MirrorTarget;
	MirrorTarget.Id = MirrorId;
	MirrorTarget.Name = MirrorId;
	MirrorTarget.Type = TEXT("CanvasPanel");
	MirrorHost.Children.Add(MoveTemp(MirrorTarget));
	MirrorHost.Children.Add(PreviewNode(TEXT("other_hosted_page"), TEXT("CanvasPanel")));
	MirrorOriginal.RootWidget.Children.Add(MoveTemp(MirrorHost));
	auto& MirrorNavigation = MirrorOriginal.MutablePrototypeNavigation();
	MirrorNavigation.OverlayHostWidgetId = TEXT("overlay_host");
	MirrorNavigation.OverlayHostWidgetName = TEXT("overlay_host");
	MirrorNavigation.ClickBindings[0].Action = EUMGPrototypeNavigationAction::Overlay;
	MirrorNavigation.ClickBindings[0].Steps[0].Kind = EUMGPrototypeNavigationStepKind::Overlay;

	Edited = MirrorOriginal;
	Edited.RootWidget.Children[1].Children[0].Name = TEXT("other_hosted_page");
	Edited.RootWidget.Children[1].Children[1].Name = MirrorId;
	TestFalse(
		TEXT("An overlay mirror cannot borrow its runtime address from another hosted page"),
		RefreshContext(MirrorOriginal, Edited).bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreviewImportCompletionClassificationTest,
	"Design2UMG.PreviewContext.ImportCompletionClassification", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreviewImportCompletionClassificationTest::RunTest(const FString& Parameters)
{
	using namespace Design2UMGEditorImportServices;

	FUMGWidgetData Data;
	TestEqual(
		TEXT("No warning is a clean completion"),
		static_cast<uint8>(ClassifyFigmaImportCompletion(Data)),
		static_cast<uint8>(EFigmaImportCompletionKind::Success));

	Data.Warnings.Add(TEXT("Unsupported blend mode was approximated"));
	TestEqual(
		TEXT("A general conversion warning is not reported as an image failure"),
		static_cast<uint8>(ClassifyFigmaImportCompletion(Data)),
		static_cast<uint8>(EFigmaImportCompletionKind::Warnings));

	Data.Warnings = {
		DesignImportPlanning::MakeResourceResolutionWarning(TEXT("download failed"))
	};
	TestEqual(
		TEXT("A remote image warning is classified as an image failure"),
		static_cast<uint8>(ClassifyFigmaImportCompletion(Data)),
		static_cast<uint8>(EFigmaImportCompletionKind::ImageFailure));

	Data.Warnings.Reset();
	DesignImportPlanning::RecordImageResolutionFailure(Data);
	TestEqual(
		TEXT("An unresolved Figma image ref is classified as an image failure"),
		static_cast<uint8>(ClassifyFigmaImportCompletion(Data)),
		static_cast<uint8>(EFigmaImportCompletionKind::ImageFailure));

	return true;
}

#endif
