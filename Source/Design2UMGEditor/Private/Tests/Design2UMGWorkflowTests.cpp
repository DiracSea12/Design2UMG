// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Design2UMGConverter.h"
#include "DesignSchema.h"
#include "Design2UMGTestUtils.h"
#include "DesignImportPipeline.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "FontResolver.h"
#include "FigmaParser.h"
#include "FigmaTestDocBuilder.h"
#include "PenParser.h"
#include "UMGGenerator.h"
#include "UMGSchemaSourceExtensions.h"
#include "PreBuildResourceNormalizer.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Extensions/WidgetBlueprintGeneratedClassExtension.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "Engine/Texture2D.h"
#include "WidgetBlueprintExtension.h"
#if WITH_AUTOMATION_WORKER
using namespace Design2UMGCoreTests;
using namespace Design2UMGTestUtils;
using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	/** Structural purity contract: no plugin-custom (Design2UMG*) extension may be baked into a generated asset. */
	template <typename TContainer>
	int32 CountDesign2UMGPrefixedExtensions(const TContainer& Extensions)
	{
		int32 Count = 0;
		for (const auto& Extension : Extensions)
		{
			const UObject* ExtensionObject = Extension;
			if (ExtensionObject && ExtensionObject->GetClass()->GetName().StartsWith(TEXT("Design2UMG")))
			{
				++Count;
			}
		}
		return Count;
	}

	FMargin ReadTextWidgetMargin(const UTextBlock* TextBlock)
	{
		if (!TextBlock)
		{
			return FMargin();
		}

		const FStructProperty* MarginProperty = FindFProperty<FStructProperty>(UTextLayoutWidget::StaticClass(), TEXT("Margin"));
		if (!MarginProperty)
		{
			return FMargin();
		}

		if (const FMargin* MarginValue = MarginProperty->ContainerPtrToValuePtr<FMargin>(TextBlock))
		{
			return *MarginValue;
		}

		return FMargin();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPenWelcomeWrapFontRegressionTest,
	"Design2UMG.Text.PencilWelcomeWrapFontRegression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPenWelcomeWrapFontRegressionTest::RunTest(const FString& Parameters)
{
	const FString PenFile = GetFixturePath(TEXT("pencil-welcome.pen"));
	if (PenFile.IsEmpty() || !FPaths::FileExists(PenFile))
	{
		AddWarning(FString::Printf(TEXT("Welcome fixture not found, skipping: %s"), *PenFile));
		return true;
	}

	FDesignDocument Document;
	FString LoadError;
	TestTrue(TEXT("Load welcome frame"), DesignImportPipeline::LoadPenDocument(PenFile, TEXT("sbChi"), Document, &LoadError));
	if (!LoadError.IsEmpty())
	{
		AddInfo(LoadError);
	}
	if (!LoadError.IsEmpty() && Document.Children.Num() == 0)
	{
		return false;
	}

	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* WelcomeBody = FindWidgetById(Data.RootWidget, TEXT("moD8f"));
	const FUMGWidgetNode* ProTips = FindWidgetById(Data.RootWidget, TEXT("vCuCj"));
	TestTrue(TEXT("Find welcome body text widget"), WelcomeBody != nullptr);
	TestTrue(TEXT("Find pro tips text widget"), ProTips != nullptr);
	if (!WelcomeBody || !ProTips)
	{
		return false;
	}

	TestTrue(TEXT("Welcome body exports typed font size"), WelcomeBody->TextStyle.FontSize > 0.0f);
	TestTrue(TEXT("Pro tips exports typed font size"), ProTips->TextStyle.FontSize > 0.0f);
	if (WelcomeBody->TextStyle.FontSize <= 0.0f || ProTips->TextStyle.FontSize <= 0.0f)
	{
		return false;
	}

	const FMargin WelcomeBodyPadding = WelcomeBody->WidgetStyle.Padding.Get(FMargin());
	const FMargin ProTipsPadding = ProTips->WidgetStyle.Padding.Get(FMargin());
	AddInfo(FString::Printf(
		TEXT("Welcome body summary: font=%.1f source=%.1f bounds=(%.1f,%.1f,%.1f,%.1f) wrap=%d wrapAt=%.1f lineHeight=%.3f fontFamily=%s typeface=%s padding=(%.1f,%.1f,%.1f,%.1f)"),
		WelcomeBody->TextStyle.FontSize,
		WelcomeBody->TextStyle.SourceFontSize,
		WelcomeBody->Bounds.X,
		WelcomeBody->Bounds.Y,
		WelcomeBody->Bounds.W,
		WelcomeBody->Bounds.H,
		WelcomeBody->Text.bAutoWrapText.Get(false) ? 1 : 0,
		WelcomeBody->Text.WrapTextAt.Get(0.0f),
		WelcomeBody->Text.LineHeightPercentage.Get(0.0f),
		*WelcomeBody->Text.FontFamily.Get(FString()),
		*WelcomeBody->Text.TypefaceFontName.Get(FString()),
		WelcomeBodyPadding.Left,
		WelcomeBodyPadding.Top,
		WelcomeBodyPadding.Right,
		WelcomeBodyPadding.Bottom));
	AddInfo(FString::Printf(
		TEXT("Pro tips summary: font=%.1f source=%.1f bounds=(%.1f,%.1f,%.1f,%.1f) wrap=%d wrapAt=%.1f lineHeight=%.3f fontFamily=%s typeface=%s padding=(%.1f,%.1f,%.1f,%.1f)"),
		ProTips->TextStyle.FontSize,
		ProTips->TextStyle.SourceFontSize,
		ProTips->Bounds.X,
		ProTips->Bounds.Y,
		ProTips->Bounds.W,
		ProTips->Bounds.H,
		ProTips->Text.bAutoWrapText.Get(false) ? 1 : 0,
		ProTips->Text.WrapTextAt.Get(0.0f),
		ProTips->Text.LineHeightPercentage.Get(0.0f),
		*ProTips->Text.FontFamily.Get(FString()),
		*ProTips->Text.TypefaceFontName.Get(FString()),
		ProTipsPadding.Left,
		ProTipsPadding.Top,
		ProTipsPadding.Right,
		ProTipsPadding.Bottom));

	const float ExpectedWelcomeBodySize =
		WelcomeBody->TextStyle.SourceFontSize * 72.0f / static_cast<float>(FontConstants::RenderDPI);
	const float ExpectedProTipsSize =
		ProTips->TextStyle.SourceFontSize * 72.0f / static_cast<float>(FontConstants::RenderDPI);
	TestTrue(TEXT("Welcome body uses the source px-to-point conversion"),
		FMath::IsNearlyEqual(WelcomeBody->TextStyle.FontSize, ExpectedWelcomeBodySize, 0.01f));
	TestTrue(TEXT("Pro tips uses the source px-to-point conversion"),
		FMath::IsNearlyEqual(ProTips->TextStyle.FontSize, ExpectedProTipsSize, 0.01f));
	TestTrue(TEXT("Welcome body and Pro tips stay on the same authoritative Rubik size"),
		FMath::IsNearlyEqual(WelcomeBody->TextStyle.FontSize, ProTips->TextStyle.FontSize, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaInstanceMetadataWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaInstanceMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaInstanceMetadataWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("inst1"), TEXT("InstanceParity"), TEXT("INSTANCE"), 16, 24, 160, 64,
			MakeTextNode(TEXT("inst1_txt"), TEXT("Label"), 28, 40, 120, 24, TEXT("Instance text"),
				TEXT("\"fontFamily\":\"Inter\",\"fontSize\":16,\"fontWeight\":500,\"textAlignHorizontal\":\"LEFT\",\"textAlignVertical\":\"TOP\",\"textDecoration\":\"NONE\",\"letterSpacing\":0,\"lineHeightUnit\":\"AUTO\",\"lineHeightPx\":0")),
			FString::Printf(
				TEXT(",\"componentId\":\"12:34\",\"componentSetId\":\"56:78\",\"mainComponentKey\":\"component-key-1\","
					 "\"componentProperties\":{\"title\":{\"type\":\"TEXT\",\"value\":\"Hello\"}},"
					 "\"variantProperties\":{\"Active\":\"True\",\"Size\":\"Small\"},"
					 "\"overrides\":[{\"id\":\"inst1_txt\",\"path\":[\"characters\"],\"value\":\"Override\"}],"
					 "\"isExposedInstance\":true,\"fills\":[%s]"),
				*MakeSolidFillJson(0.18, 0.35, 0.95))),
		TEXT(",\"components\":{\"12:34\":{\"key\":\"nav_item_component\",\"name\":\"NavItem\",\"componentPropertyDefinitions\":{\"title\":{\"type\":\"TEXT\"}}}},"
			 "\"componentSets\":{\"56:78\":{\"key\":\"nav_item_variants\",\"name\":\"NavItemVariants\",\"variantGroupProperties\":{\"Active\":{\"values\":[\"True\",\"False\"]}}}}"));

	FDesignDocument Document;
	TestTrue(TEXT("Parse figma instance metadata fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());
	TestTrue(TEXT("Converted widget data preserves Figma component catalog"), Data.SourceDocument().HasAnyComponentCatalog());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaInstanceMetadata");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build figma instance metadata widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated instance metadata class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	// The component catalog lives in the IR SourceDocument (conversion-level contract); the generated asset
	// itself must stay structurally pure: no plugin-custom (Design2UMG*) extension is baked in, and the former
	// Figma widget-metadata records are no longer collected.
	TestEqual(TEXT("Generated class carries no plugin-custom extensions"),
		CountDesign2UMGPrefixedExtensions(GeneratedClass->GetExtensions(UWidgetBlueprintGeneratedClassExtension::StaticClass())), 0);
	TestEqual(TEXT("Widget blueprint carries no plugin-custom extensions"),
		CountDesign2UMGPrefixedExtensions(UWidgetBlueprintExtension::GetExtensions(Blueprint)), 0);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Instance metadata widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestTrue(TEXT("Runtime widget tree still resolves the instance widget name"),
		WidgetInstance->WidgetTree->FindWidget(TEXT("InstanceParity")) != nullptr);
	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextAlignVerticalRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.TextAlignVerticalRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FTextAlignVerticalRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = TEXT(R"({
		"children": [
			{
				"id": "root",
				"type": "frame",
				"width": 240,
				"height": 80,
				"layout": "horizontal",
				"alignItems": "start",
				"children": [
					{
						"id": "label",
						"type": "text",
						"content": "Button",
						"width": 140,
						"height": 40,
						"fill": "#FFFFFF",
						"fontFamily": "Geist",
						"fontSize": 14,
						"textAlignVertical": "middle"
					}
				]
			}
		]
	})");

	FDesignDocument Document;
	TestTrue(TEXT("Parse textAlignVertical runtime fixture"), FPenParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/TextAlignVerticalRuntime");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build textAlignVertical widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated textAlignVertical class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("textAlignVertical widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();

	UTextBlock* Label = Cast<UTextBlock>(WidgetInstance->WidgetTree->FindWidget(TEXT("label")));
	TestTrue(TEXT("textAlignVertical TextBlock exists"), Label != nullptr);
	if (!Label)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	const FMargin LabelMargin = ReadTextWidgetMargin(Label);
	TestTrue(TEXT("Runtime TextBlock receives positive top margin"), LabelMargin.Top > 0.5f);
	TestTrue(TEXT("Runtime TextBlock receives positive bottom margin"), LabelMargin.Bottom > 0.5f);
	TestTrue(TEXT("Runtime TextBlock center alignment keeps margins symmetric"),
		FMath::IsNearlyEqual(LabelMargin.Top, LabelMargin.Bottom, 0.75f));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaOverflowDirectionRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaOverflowDirectionRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaOverflowDirectionRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("scroll_frame"), TEXT("ScrollFrame"), TEXT("FRAME"), 0, 0, 320, 240,
			FString::Printf(TEXT("%s,%s"),
				*MakeLeafNode(TEXT("row1"), TEXT("Row1"), TEXT("RECTANGLE"), 0, 0, 320, 80,
					FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.3, 0.4))),
				*MakeLeafNode(TEXT("row2"), TEXT("Row2"), TEXT("RECTANGLE"), 0, 92, 320, 80,
					FString::Printf(TEXT(",\"fills\":[%s]"), *MakeSolidFillJson(0.4, 0.3, 0.2)))),
			TEXT(",\"layoutMode\":\"VERTICAL\",\"itemSpacing\":12,\"overflowDirection\":\"VERTICAL_SCROLLING\"")));

	FDesignDocument Document;
	TestTrue(TEXT("Parse scrollable figma workflow fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaOverflowDirection");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build figma overflow widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated overflow class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Overflow widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();

	UScrollBox* ScrollBox = Cast<UScrollBox>(WidgetInstance->WidgetTree->FindWidget(TEXT("ScrollFrame")));
	TestTrue(TEXT("Scrollable frame instantiates as ScrollBox"), ScrollBox != nullptr);
	if (!ScrollBox)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("ScrollBox orientation is vertical"), ScrollBox->GetOrientation(), Orient_Vertical);
	TestEqual(TEXT("ScrollBox keeps child count"), ScrollBox->GetChildrenCount(), 2);
	TestEqual(TEXT("ScrollBox hides scrollbar by default"), ScrollBox->GetScrollBarVisibility(), ESlateVisibility::Hidden);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaGridSlotSemanticsRuntimeWorkflowTest,
	"Design2UMG.WidgetBlueprint.FigmaGridSlotSemanticsRuntime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaGridSlotSemanticsRuntimeWorkflowTest::RunTest(const FString& Parameters)
{
	const FString Json = MakeFigmaDocJson(
		MakeContainerNode(TEXT("grid_frame"), TEXT("grid_frame"), TEXT("FRAME"), 0, 0, 320, 240,
			FString::Printf(TEXT("%s,%s,%s"),
				*MakeLeafNode(TEXT("head_card"), TEXT("head_card"), TEXT("RECTANGLE"), 0, 0, 100, 40,
					FString::Printf(TEXT(",\"gridRowAnchorIndex\":0,\"gridColumnAnchorIndex\":0,\"fills\":[%s]"), *MakeSolidFillJson(0.2, 0.3, 0.4))),
				*MakeLeafNode(TEXT("tail_card"), TEXT("tail_card"), TEXT("RECTANGLE"), 116, 0, 100, 40,
					FString::Printf(TEXT(",\"gridRowAnchorIndex\":0,\"gridColumnAnchorIndex\":1,\"fills\":[%s]"), *MakeSolidFillJson(0.4, 0.3, 0.2))),
				*MakeLeafNode(TEXT("hero_card"), TEXT("hero_card"), TEXT("RECTANGLE"), 0, 64, 216, 40,
					FString::Printf(TEXT(",\"gridRowAnchorIndex\":1,\"gridColumnAnchorIndex\":0,\"gridColumnSpan\":2,\"fills\":[%s]"), *MakeSolidFillJson(0.1, 0.6, 0.5)))),
			TEXT(",\"layoutMode\":\"GRID\",\"gridRowCount\":2,\"gridColumnCount\":2,\"gridRowGap\":24,\"gridColumnGap\":12,"
				 "\"gridChildHorizontalAlign\":\"CENTER\",\"gridChildVerticalAlign\":\"MAX\"")));

	FDesignDocument Document;
	TestTrue(TEXT("Parse figma grid runtime fixture"), FFigmaParser::ParseDocument(Json, Document));
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, FDesign2UMGConversionSettings::Defaults());

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/FigmaGridSlotSemantics");
	DeleteGeneratedAssetsAtPath(OutputPath);

	UWidgetBlueprint* Blueprint = FUMGGenerator::BuildWidgetBlueprint(Data, OutputPath);
	TestTrue(TEXT("Build figma grid widget blueprint"), Blueprint != nullptr);
	if (!Blueprint)
	{
		return false;
	}

	FUMGGenerator::SaveWidgetBlueprint(Blueprint);
	UWidgetBlueprintGeneratedClass* GeneratedClass = Cast<UWidgetBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	TestTrue(TEXT("Generated figma grid class exists"), GeneratedClass != nullptr);
	if (!GeneratedClass)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestTrue(TEXT("Editor world exists"), World != nullptr);
	UUserWidget* WidgetInstance = World ? CreateWidget<UUserWidget>(World, GeneratedClass) : nullptr;
	TestTrue(TEXT("Grid widget instance created"), WidgetInstance != nullptr);
	if (!WidgetInstance || !WidgetInstance->WidgetTree)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	WidgetInstance->TakeWidget();

	UGridPanel* GridPanel = Cast<UGridPanel>(WidgetInstance->WidgetTree->FindWidget(TEXT("grid_frame")));
	UWidget* TailCard = WidgetInstance->WidgetTree->FindWidget(TEXT("tail_card"));
	UWidget* HeroCard = WidgetInstance->WidgetTree->FindWidget(TEXT("hero_card"));
	// The SizeBox wrapper carries the grid slot for each wrapped card.
	UWidget* TailWrapper = WidgetInstance->WidgetTree->FindWidget(TEXT("tail_card_sz"));
	UWidget* HeroWrapper = WidgetInstance->WidgetTree->FindWidget(TEXT("hero_card_sz"));
	TestTrue(TEXT("Grid frame instantiates as GridPanel"), GridPanel != nullptr);
	TestTrue(TEXT("Tail card runtime widget exists"), TailCard != nullptr);
	TestTrue(TEXT("Hero card runtime widget exists"), HeroCard != nullptr);
	TestTrue(TEXT("Tail card is wrapped in a runtime SizeBox"), TailWrapper != nullptr);
	TestTrue(TEXT("Hero card is wrapped in a runtime SizeBox"), HeroWrapper != nullptr);
	if (!GridPanel || !TailCard || !HeroCard || !TailWrapper || !HeroWrapper)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	UGridSlot* TailSlot = Cast<UGridSlot>(TailWrapper->Slot);
	UGridSlot* HeroSlot = Cast<UGridSlot>(HeroWrapper->Slot);
	TestTrue(TEXT("Tail slot is UGridSlot"), TailSlot != nullptr);
	TestTrue(TEXT("Hero slot is UGridSlot"), HeroSlot != nullptr);
	if (!TailSlot || !HeroSlot)
	{
		DeleteGeneratedAssetsAtPath(OutputPath);
		return false;
	}

	TestEqual(TEXT("Tail runtime row"), TailSlot->GetRow(), 0);
	TestEqual(TEXT("Tail runtime column"), TailSlot->GetColumn(), 1);
	TestEqual(TEXT("Tail runtime horizontal align"), TailSlot->GetHorizontalAlignment(), HAlign_Center);
	TestEqual(TEXT("Tail runtime vertical align"), TailSlot->GetVerticalAlignment(), VAlign_Bottom);
	const FMargin TailPadding = TailSlot->GetPadding();
	TestTrue(TEXT("Tail runtime inner left gap"), FMath::IsNearlyEqual(TailPadding.Left, 6.0f, 0.01f));
	TestTrue(TEXT("Tail runtime bottom gap"), FMath::IsNearlyEqual(TailPadding.Bottom, 12.0f, 0.01f));

	TestEqual(TEXT("Hero runtime row"), HeroSlot->GetRow(), 1);
	TestEqual(TEXT("Hero runtime column"), HeroSlot->GetColumn(), 0);
	TestEqual(TEXT("Hero runtime column span"), HeroSlot->GetColumnSpan(), 2);
	TestEqual(TEXT("Hero runtime horizontal align"), HeroSlot->GetHorizontalAlignment(), HAlign_Center);
	TestEqual(TEXT("Hero runtime vertical align"), HeroSlot->GetVerticalAlignment(), VAlign_Bottom);
	const FMargin HeroPadding = HeroSlot->GetPadding();
	TestTrue(TEXT("Hero runtime top gap"), FMath::IsNearlyEqual(HeroPadding.Top, 12.0f, 0.01f));
	TestTrue(TEXT("Hero runtime right outer edge has no extra gap"), FMath::IsNearlyEqual(HeroPadding.Right, 0.0f, 0.01f));

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFitContentMinimumExpandsToContentTest,
	"Design2UMG.Converter.FitContentMinimumExpandsToContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFitContentMinimumExpandsToContentTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;
	const TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("fit-min-root");
	Root->Name = TEXT("FitMinRoot");
	Root->Type = EDesignNodeType::Frame;
	Root->LayoutData.Layout = EDesignLayout::Vertical;
	Root->Transform.Width = FDesignSize::Fixed(320.0f);
	Root->Transform.Height = FDesignSize::Fit();
	Root->Transform.bHasWidth = true;
	const TSharedPtr<FDesignNode> Row = MakeShared<FDesignNode>();
	Row->Id = TEXT("fit-min-row");
	Row->Name = TEXT("FitMinRow");
	Row->Type = EDesignNodeType::Frame;
	Row->LayoutData.Layout = EDesignLayout::Vertical;
	Row->Transform.Width = FDesignSize::Fixed(320.0f);
	Row->Transform.Height = FDesignSize::Fit(44.0f);
	Row->Transform.bHasWidth = true;
	Row->Transform.bHasHeight = true;
	const TSharedPtr<FDesignNode> Content = MakeShared<FDesignNode>();
	Content->Id = TEXT("fit-min-content");
	Content->Name = TEXT("FitMinContent");
	Content->Type = EDesignNodeType::Frame;
	Content->LayoutData.Layout = EDesignLayout::Vertical;
	Content->Transform.Width = FDesignSize::Fixed(320.0f);
	Content->Transform.Height = FDesignSize::Fixed(56.0f);
	Content->Transform.bHasWidth = true;
	Content->Transform.bHasHeight = true;
	Row->Children.Add(Content);
	Root->Children.Add(Row);
	Doc.Children.Add(Root);
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* RootWidget = FindWidgetById(Data.RootWidget, TEXT("fit-min-root"));
	const FUMGWidgetNode* RowWidget = FindWidgetById(Data.RootWidget, TEXT("fit-min-row_sz"));
	TestTrue(TEXT("Find fit-content minimum root widget"), RootWidget != nullptr);
	TestTrue(TEXT("Find fit-content minimum row SizeBox wrapper"), RowWidget != nullptr);
	if (!RootWidget || !RowWidget)
	{
		return false;
	}
	TestTrue(TEXT("Root fit-content height should grow beyond the minimum fallback"),
		RootWidget->Bounds.H >= 56.0f - 0.5f);
	const FString HeightOverride = GetWidgetProperty(*RowWidget, TEXT("HeightOverride"));
	const bool bHasHeightOverride = !HeightOverride.IsEmpty();
	const FString MinDesiredHeight = GetWidgetProperty(*RowWidget, TEXT("MinDesiredHeight"));
	const bool bHasMinDesiredHeight = !MinDesiredHeight.IsEmpty();
	TestTrue(TEXT("Flow fit-content(min) row should export a concrete height constraint"),
		bHasHeightOverride || bHasMinDesiredHeight);
	if (bHasHeightOverride)
	{
		TestTrue(TEXT("HeightOverride should be at least the larger child content height"),
			FCString::Atof(*HeightOverride) >= 56.0f - 0.5f);
	}
	if (bHasMinDesiredHeight)
	{
		TestTrue(TEXT("MinDesiredHeight should be at least the larger child content height"),
			FCString::Atof(*MinDesiredHeight) >= 56.0f - 0.5f);
	}
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFillWidthSingleLineTextDoesNotInflateFitHeightTest,
	"Design2UMG.Converter.FillWidthSingleLineTextDoesNotInflateFitHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFillWidthSingleLineTextDoesNotInflateFitHeightTest::RunTest(const FString& Parameters)
{
	FDesignDocument Doc;
	const TSharedPtr<FDesignNode> Root = MakeShared<FDesignNode>();
	Root->Id = TEXT("text-fit-root");
	Root->Name = TEXT("TextFitRoot");
	Root->Type = EDesignNodeType::Frame;
	Root->LayoutData.Layout = EDesignLayout::Vertical;
	Root->Transform.Width = FDesignSize::Fixed(160.0f);
	Root->Transform.Height = FDesignSize::Fit();
	Root->Transform.bHasWidth = true;
	const TSharedPtr<FDesignNode> HeaderCell = MakeShared<FDesignNode>();
	HeaderCell->Id = TEXT("header-cell");
	HeaderCell->Name = TEXT("HeaderCell");
	HeaderCell->Type = EDesignNodeType::Frame;
	HeaderCell->LayoutData.Layout = EDesignLayout::Horizontal;
	HeaderCell->Transform.Width = FDesignSize::Fixed(160.0f);
	HeaderCell->Transform.Height = FDesignSize::Fit(44.0f);
	HeaderCell->Transform.bHasWidth = true;
	HeaderCell->Transform.bHasHeight = true;
	{
		FDesignPadding P; P.Top = P.Right = P.Bottom = P.Left = 12.0f;
		HeaderCell->LayoutData.SetPadding(P);
	}
	HeaderCell->LayoutData.AlignItems = EDesignAlignItems::Center;
	const TSharedPtr<FDesignNode> HeaderText = MakeShared<FDesignNode>();
	HeaderText->Id = TEXT("header-text");
	HeaderText->Name = TEXT("HeaderText");
	HeaderText->Type = EDesignNodeType::Text;
	HeaderText->Transform.Width = FDesignSize::Fill();
	HeaderText->Transform.Height = FDesignSize::Fit();
	HeaderText->Text.Content = TEXT("Status");
	HeaderText->Text.FontFamily = TEXT("Inter");
	HeaderText->Text.FontSize = 14.0f;
	HeaderText->Text.FontWeight = TEXT("normal");
	HeaderText->Text.LineHeight = 1.4285714285714286f;
	HeaderText->Text.TextGrowth = EDesignTextGrowth::FixedWidth;
	HeaderCell->Children.Add(HeaderText);
	Root->Children.Add(HeaderCell);
	Doc.Children.Add(Root);
	const FUMGWidgetData Data = FDesign2UMGConverter::Convert(Doc, FDesign2UMGConversionSettings::Defaults());
	const FUMGWidgetNode* RootWidget = FindWidgetById(Data.RootWidget, TEXT("text-fit-root"));
	const FUMGWidgetNode* HeaderCellWidget = FindWidgetById(Data.RootWidget, TEXT("header-cell_sz"));
	TestTrue(TEXT("Find single-line text fit root widget"), RootWidget != nullptr);
	TestTrue(TEXT("Find header cell SizeBox wrapper"), HeaderCellWidget != nullptr);
	if (!RootWidget || !HeaderCellWidget)
	{
		return false;
	}
	const FString HeightOverride = GetWidgetProperty(*HeaderCellWidget, TEXT("HeightOverride"));
	const bool bHasHeightOverride = !HeightOverride.IsEmpty();
	const FString MinDesiredHeight = GetWidgetProperty(*HeaderCellWidget, TEXT("MinDesiredHeight"));
	const bool bHasMinDesiredHeight = !MinDesiredHeight.IsEmpty();
	TestTrue(TEXT("Header cell should export a height constraint"),
		bHasHeightOverride || bHasMinDesiredHeight);
	const float ExportedHeight = bHasHeightOverride
		? FCString::Atof(*HeightOverride)
		: (bHasMinDesiredHeight ? FCString::Atof(*MinDesiredHeight) : 0.0f);
	TestTrue(TEXT("Single-line fill-width text should keep the 44px fit-content minimum"),
		FMath::IsNearlyEqual(ExportedHeight, 44.0f, 0.5f));
	TestTrue(TEXT("Root fit height should remain at the 44px minimum"),
		FMath::IsNearlyEqual(RootWidget->Bounds.H, 44.0f, 0.5f));
	return true;
}
namespace
{
	struct FNormalizerSnapshot
	{
		FString Type;
		FString Id;
		FString Name;
		FString PathGeometry;
		bool bHasGeometry = false;
		FString MaterialRef;
		bool bHasMaterialRef = false;
		FString TexturePath;
		bool bHasTexturePath = false;
		bool bHasImageMode = false;
		bool bHasImageTransform = false;
	};

	FNormalizerSnapshot SnapNode(const FUMGWidgetNode& Node)
	{
		FNormalizerSnapshot S;
		S.Type = Node.Type;
		S.Id = Node.Id;
		S.Name = Node.Name;
		S.PathGeometry = Node.Path.Geometry.Get(FString());
		S.bHasGeometry = Node.Path.Geometry.IsSet();
		S.MaterialRef = Node.Brush.MaterialRef.Get(FString());
		S.bHasMaterialRef = Node.Brush.MaterialRef.IsSet();
		S.TexturePath = Node.Brush.Image.TexturePath.Get(FString());
		S.bHasTexturePath = Node.Brush.Image.TexturePath.IsSet();
		S.bHasImageMode = Node.Brush.Image.Mode.IsSet() || Node.WidgetStyle.ImageMode.IsSet();
		S.bHasImageTransform = Node.Brush.Image.Transform.IsSet() || Node.WidgetStyle.ImageTransform.IsSet();
		return S;
	}

	FUMGWidgetNode MakePathImageNode(const FString& Id, const FString& Geometry)
	{
		FUMGWidgetNode Node;
		Node.Type = TEXT("Image");
		Node.Id = Id;
		Node.Name = Id;
		if (!Geometry.IsEmpty())
		{
			Node.Path.Geometry = Geometry;
		}
		Node.Path.FillRule = TEXT("nonzero");
		Node.Path.FillColor = FLinearColor::White;
		Node.WidgetStyle.WidthOverride = 32.0f;
		Node.WidgetStyle.HeightOverride = 32.0f;
		return Node;
	}

	FUMGWidgetNode MakeProceduralNode(const FString& Id, const FString& MaterialRef)
	{
		FUMGWidgetNode Node;
		Node.Type = TEXT("Image");
		Node.Id = Id;
		Node.Name = Id;
		if (!MaterialRef.IsEmpty())
		{
			Node.Brush.MaterialRef = MaterialRef;
		}
		Node.WidgetStyle.WidthOverride = 32.0f;
		Node.WidgetStyle.HeightOverride = 32.0f;
		return Node;
	}

	FUMGWidgetNode MakeImageTransformNode(const FString& Id)
	{
		FUMGWidgetNode Node;
		Node.Type = TEXT("Border");
		Node.Id = Id;
		Node.Name = Id;
		Node.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
		Node.WidgetStyle.ImageMode = TEXT("Crop");
		Node.WidgetStyle.ImageTransform = TEXT("0.75,0.1,0.05,-0.1,0.65,0.2");
		Node.WidgetStyle.WidthOverride = 8.0f;
		Node.WidgetStyle.HeightOverride = 6.0f;
		return Node;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreBuildNormalizerPathTextureBeforeAfterDiffTest,
	"Design2UMG.PreBuildResourceNormalizer.PathTexture.BeforeAfterDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreBuildNormalizerPathTextureBeforeAfterDiffTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI) return true;

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("norm_path_root");
	Root.Name = TEXT("norm_path_root");
	Root.Bounds = {0, 0, 64, 64};
	Root.Children.Add(MakePathImageNode(TEXT("path_node"), TEXT("M 0 0 L 32 0 L 32 32 Z")));

	const FNormalizerSnapshot Before = SnapNode(Root.Children[0]);
	TestTrue(TEXT("Before: bHasGeometry"), Before.bHasGeometry);
	TestFalse(TEXT("Before: bHasTexturePath"), Before.bHasTexturePath);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/NormPathTexture");
	FPreBuildResourceNormalizer::NormalizePathTextures(Root, OutputPath / TEXT("D2U_PathTextures"), TEXT("norm_path"));

	const FNormalizerSnapshot After = SnapNode(Root.Children[0]);
	TestEqual(TEXT("After: type unchanged"), After.Type, Before.Type);
	TestEqual(TEXT("After: id unchanged"), After.Id, Before.Id);
	TestEqual(TEXT("After: name unchanged"), After.Name, Before.Name);
	TestFalse(TEXT("After: bHasGeometry cleared"), After.bHasGeometry);
	TestTrue(TEXT("After: bHasTexturePath set"), After.bHasTexturePath);
	TestFalse(TEXT("After: TexturePath non-empty"), After.TexturePath.IsEmpty());
	TestFalse(TEXT("After: MaterialRef not set"), After.bHasMaterialRef);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreBuildNormalizerProceduralTextureBeforeAfterDiffTest,
	"Design2UMG.PreBuildResourceNormalizer.ProceduralTexture.BeforeAfterDiff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreBuildNormalizerProceduralTextureBeforeAfterDiffTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI) return true;

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("norm_proc_root");
	Root.Name = TEXT("norm_proc_root");
	Root.Bounds = {0, 0, 64, 64};
	Root.Children.Add(MakeProceduralNode(TEXT("proc_node"), TEXT("RoundedRect")));

	const FNormalizerSnapshot Before = SnapNode(Root.Children[0]);
	TestTrue(TEXT("Before: bHasMaterialRef"), Before.bHasMaterialRef);
	TestFalse(TEXT("Before: bHasTexturePath"), Before.bHasTexturePath);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/NormProcTexture");
	FPreBuildResourceNormalizer::NormalizeProceduralTextures(Root, OutputPath / TEXT("D2U_ProceduralTextures"), TEXT("norm_proc"));

	const FNormalizerSnapshot After = SnapNode(Root.Children[0]);
	TestEqual(TEXT("After: type unchanged"), After.Type, Before.Type);
	TestEqual(TEXT("After: id unchanged"), After.Id, Before.Id);
	TestEqual(TEXT("After: name unchanged"), After.Name, Before.Name);
	TestFalse(TEXT("After: bHasMaterialRef cleared"), After.bHasMaterialRef);
	TestTrue(TEXT("After: bHasTexturePath set"), After.bHasTexturePath);
	TestFalse(TEXT("After: TexturePath non-empty"), After.TexturePath.IsEmpty());

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreBuildNormalizerPathTextureCacheDedupTest,
	"Design2UMG.PreBuildResourceNormalizer.PathTexture.CacheDedup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreBuildNormalizerPathTextureCacheDedupTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI) return true;

	const FString SharedGeometry = TEXT("M 0 0 L 32 0 L 32 32 Z");

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("norm_dedup_root");
	Root.Name = TEXT("norm_dedup_root");
	Root.Bounds = {0, 0, 64, 64};
	Root.Children.Add(MakePathImageNode(TEXT("dedup_a"), SharedGeometry));
	Root.Children.Add(MakePathImageNode(TEXT("dedup_b"), SharedGeometry));

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/NormPathDedup");
	FPreBuildResourceNormalizer::NormalizePathTextures(Root, OutputPath / TEXT("D2U_PathTextures"), TEXT("norm_dedup"));

	const FNormalizerSnapshot AfterA = SnapNode(Root.Children[0]);
	const FNormalizerSnapshot AfterB = SnapNode(Root.Children[1]);

	TestTrue(TEXT("A: bHasTexturePath set"), AfterA.bHasTexturePath);
	TestTrue(TEXT("B: bHasTexturePath set"), AfterB.bHasTexturePath);
	TestFalse(TEXT("A: geometry cleared"), AfterA.bHasGeometry);
	TestFalse(TEXT("B: geometry cleared"), AfterB.bHasGeometry);
	TestEqual(TEXT("Dedup: same geometry yields same asset path"), AfterA.TexturePath, AfterB.TexturePath);

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPreBuildNormalizerImageTransformTextureTest,
	"Design2UMG.PreBuildResourceNormalizer.ImageTransform.PersistedAndDeduplicated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPreBuildNormalizerImageTransformTextureTest::RunTest(const FString& Parameters)
{
	if (GUsingNullRHI) return true;

	FUMGWidgetNode Root;
	Root.Type = TEXT("CanvasPanel");
	Root.Id = TEXT("norm_image_transform_root");
	Root.Name = TEXT("norm_image_transform_root");
	Root.Bounds = {0, 0, 64, 64};
	Root.Children.Add(MakeImageTransformNode(TEXT("image_transform_a")));
	Root.Children.Add(MakeImageTransformNode(TEXT("image_transform_b")));

	const FNormalizerSnapshot Before = SnapNode(Root.Children[0]);
	TestTrue(TEXT("Before: image mode is present"), Before.bHasImageMode);
	TestTrue(TEXT("Before: image transform is present"), Before.bHasImageTransform);

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/NormImageTransform");
	FPreBuildResourceNormalizer::NormalizeImageTransformTextures(
		Root,
		OutputPath / TEXT("D2U_ImageTransforms"),
		TEXT("norm_image_transform"));

	const FNormalizerSnapshot AfterA = SnapNode(Root.Children[0]);
	const FNormalizerSnapshot AfterB = SnapNode(Root.Children[1]);
	TestTrue(TEXT("A: transformed texture path is set"), AfterA.bHasTexturePath);
	TestTrue(TEXT("B: transformed texture path is set"), AfterB.bHasTexturePath);
	TestEqual(TEXT("Equivalent transforms reuse the same persisted texture"), AfterA.TexturePath, AfterB.TexturePath);
	TestFalse(TEXT("A: baked image mode is cleared"), AfterA.bHasImageMode);
	TestFalse(TEXT("A: baked image transform is cleared"), AfterA.bHasImageTransform);
	TestFalse(TEXT("B: baked image mode is cleared"), AfterB.bHasImageMode);
	TestFalse(TEXT("B: baked image transform is cleared"), AfterB.bHasImageTransform);

	UTexture2D* PersistedTexture = LoadObject<UTexture2D>(nullptr, *AfterA.TexturePath);
	if (TestNotNull(TEXT("Transformed texture is loadable as an asset"), PersistedTexture))
	{
		TestFalse(TEXT("Persisted texture is not duplicate-transient"), PersistedTexture->HasAnyFlags(RF_DuplicateTransient));
		TestTrue(TEXT("Persisted texture is public"), PersistedTexture->HasAnyFlags(RF_Public));
		TestTrue(TEXT("Persisted texture is standalone"), PersistedTexture->HasAnyFlags(RF_Standalone));
		TestEqual(TEXT("Persisted texture width matches the requested output"), PersistedTexture->GetSizeX(), 8);
		TestEqual(TEXT("Persisted texture height matches the requested output"), PersistedTexture->GetSizeY(), 6);
	}

	DeleteGeneratedAssetsAtPath(OutputPath);
	return true;
}

#endif // WITH_AUTOMATION_WORKER
